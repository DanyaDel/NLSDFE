#include "TestUtil.h"
#include "ScriptSystem.h"
#include "GameApi.h"
#include "UiLayout.h"
#include "Scene.h"
#include "Particles.h"
#include "Animation.h"
#include "Navigation.h"
#include "Audio.h"
#include <filesystem>
#include <fstream>

using namespace nls;

// Собирает скрипт и вызывает test() -> int
static int runAs(const char* type, const std::string& body, std::string* error) {
    Program program;
    std::vector<CompileError> errors;
    if (!compile(std::string("func test() -> ") + type + " { " + body + " }", program, errors)) {
        if (error) *error = errors.empty() ? "?" : errors[0].message;
        else for (const auto& e : errors) std::printf("  compile error %d:%d: %s\n", e.line, e.column, e.message.c_str());
        return -99999;
    }
    VM vm;
    vm.init(&program);
    Value result;
    if (!vm.call(program.findFunction("test", false), nullptr, 0, &result)) {
        if (error) *error = vm.error; else std::printf("  runtime error: %s\n", vm.error.c_str());
        return -99999;
    }
    return result.i;
}
static int runInt(const std::string& body, std::string* error = nullptr) { return runAs("int", body, error); }
// test() -> node: индекс узла
static int runNode(const std::string& body, std::string* error = nullptr) { return runAs("node", body, error); }

void runGameTests() {
    registerEngineBindings();
    std::string dir = (std::filesystem::temp_directory_path() / "nlsdfe_game_test").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/ui");
    std::filesystem::create_directories(dir + "/assets/prefabs");
    auto savedResolve = host.resolvePath;
    host.resolvePath = [dir](const std::string& p) { return dir + "/" + p; };

    TEST_SECTION("game: destroy and slot reuse");
    resetScene();
    game::reset();
    {
        int parent = createNode(SDF_SPHERE, "Enemy");
        int child = createNode(SDF_BOX, "Gun");
        reparent(child, parent);
        int other = createNode(SDF_BOX, "Watcher");
        sceneGraph[other].triggerTarget = parent;
        int before = (int)sceneGraph.size();
        updateSceneCaches();

        // Копия забирает потомков и ссылки внутри поддерева
        sceneGraph[child].triggerTarget = parent;
        int copy = runNode("let e = scene.find(\"Enemy\")\n let c = scene.spawn(e)\n return c");
        CHECK(copy == before);
        CHECK((int)sceneGraph.size() == before + 2);
        if ((int)sceneGraph.size() == before + 2) {
            CHECK(sceneGraph[copy].children.size() == 1);
            int copyChild = sceneGraph[copy].children.empty() ? -1 : sceneGraph[copy].children[0];
            CHECK(copyChild >= 0 && sceneGraph[copyChild].name == "Gun" && sceneGraph[copyChild].parent == copy);
            CHECK(copyChild >= 0 && sceneGraph[copyChild].triggerTarget == copy);
        }

        // Удаление прячет сразу, место освобождается после flushDestroyed
        CHECK(runInt("let e = scene.find(\"Enemy\")\n e.destroy()\n if e.isAlive() { return 1 }\n return 0") == 0);
        CHECK(sceneGraph[parent].destroyed && sceneGraph[child].destroyed && !sceneGraph[parent].active);
        uint32_t generation = sceneGraph[parent].generation;
        flushDestroyed();
        CHECK(sceneGraph[parent].generation == generation + 1);
        CHECK(sceneGraph[other].triggerTarget == -1);          // Ссылка на удалённый узел обнулена
        CHECK(sceneGraph[parent].children.empty());

        // Новый узел занимает освобождённые места, сцена не растёт
        int size = (int)sceneGraph.size();
        int again = runNode("return scene.spawn(scene.get(" + std::to_string(copy) + "))");
        CHECK(again == parent || again == child);
        CHECK((int)sceneGraph.size() == size);
        CHECK(again >= 0 && !sceneGraph[again].destroyed && sceneGraph[again].children.size() == 1);

        // Тысяча выстрелов не упирается в лимит узлов
        bool ok = true;
        for (int i = 0; i < 1000 && ok; i++) {
            int bullet = spawnNodes(copySubtree(other));
            ok = bullet >= 0;
            destroyNode(bullet);
            flushDestroyed();
        }
        CHECK(ok);
        CHECK((int)sceneGraph.size() <= size + 1);

        // Когда места нет, spawn возвращает null-узел, а не ломает сцену
        while (spawnNodes(copySubtree(other)) >= 0) {}
        CHECK((int)sceneGraph.size() == MAX_OBJECTS);
        CHECK(runNode("return scene.spawn(scene.find(\"Watcher\"))") == -1);
    }

    TEST_SECTION("game: prefabs");
    resetScene();
    game::reset();
    {
        // Префаб из двух корней: при вставке они собираются под один узел
        int a = createNode(SDF_SPHERE, "Body");
        int b = createNode(SDF_BOX, "Hat");
        sceneGraph[a].emitter.enabled = true;
        sceneGraph[a].emitter.rate = 77.0f;
        Keyframe k; k.time = 1.5f; k.position = vec3(1, 2, 3);
        sceneGraph[b].animation.enabled = true;
        sceneGraph[b].animation.keys = { Keyframe(), k };
        std::vector<SceneNode> keep = sceneGraph;
        sceneGraph = { sceneGraph[a], sceneGraph[b] };
        CHECK(saveScene(dir + "/assets/prefabs/snowman.nos", {}));
        sceneGraph = keep;
        int size = (int)sceneGraph.size();

        int root = runNode("return scene.instantiate(\"snowman\", vec3(4, 5, 6))");
        CHECK(root >= 0);
        CHECK((int)sceneGraph.size() == size + 3);
        if (root >= 0 && (int)sceneGraph.size() == size + 3) {
            CHECK_NEAR(sceneGraph[root].localPosition.y, 5.0, 1e-6);
            CHECK(sceneGraph[root].children.size() == 2);
            // Компоненты версии 5 пережили запись и чтение
            int body = runNode("return scene.get(" + std::to_string(root) + ").child(\"Body\")");
            int hat = runNode("return scene.get(" + std::to_string(root) + ").child(\"Hat\")");
            CHECK(body >= 0 && sceneGraph[body].emitter.enabled);
            CHECK(body >= 0 && std::fabs(sceneGraph[body].emitter.rate - 77.0f) < 1e-6f);
            CHECK(hat >= 0 && sceneGraph[hat].animation.keys.size() == 2);
            CHECK(hat >= 0 && sceneGraph[hat].animation.keys.size() == 2 && std::fabs(sceneGraph[hat].animation.keys[1].position.z - 3.0f) < 1e-6f);
        }
        std::string error;
        CHECK(runNode("return scene.instantiate(\"nothing\", vec3(0, 0, 0))", &error) == -99999 && error.find("not found") != std::string::npos);
    }

    TEST_SECTION("game: keyframe animation");
    resetScene();
    {
        SceneNode& n = sceneGraph[0];
        NodeAnimation& a = n.animation;
        a = NodeAnimation();
        a.enabled = true;
        Keyframe k0, k1, k2;
        k0.time = 0.0f; k0.position = vec3(0, 0, 0);
        k2.time = 2.0f; k2.position = vec3(4, 0, 0);
        k1.time = 1.0f; k1.position = vec3(0, 2, 0);
        anim::insertKey(a, k2); anim::insertKey(a, k0);
        CHECK(anim::insertKey(a, k1) == 1);                    // Ключи встают по времени
        k1.position = vec3(0, 3, 0);
        CHECK(anim::insertKey(a, k1) == 1 && a.keys.size() == 3);   // То же время — замена
        CHECK_NEAR(anim::length(a), 2.0, 1e-6);
        CHECK_NEAR(anim::sample(a, 0.5f).position.y, 1.5, 1e-5);
        CHECK_NEAR(anim::sample(a, 9.0f).position.x, 4.0, 1e-6);

        // Один проход: останавливается на последнем ключе
        a.loop = false;
        anim::startAll();
        CHECK(a.playing);
        for (int i = 0; i < 30; i++) anim::update(0.1f);
        CHECK(!a.playing);
        CHECK_NEAR(n.localPosition.x, 4.0, 1e-5);
        // Цикл: время заворачивается
        a.loop = true;
        anim::play(n, true);
        for (int i = 0; i < 25; i++) anim::update(0.1f);
        CHECK(a.playing && a.time < 2.0f);
        CHECK_NEAR(a.time, 0.5, 1e-3);
        // Туда и обратно: на времени 3.0 поза та же, что на 1.0
        a.pingPong = true;
        anim::play(n, true);
        for (int i = 0; i < 30; i++) anim::update(0.1f);
        CHECK_NEAR(n.localPosition.y, 3.0, 0.05);
        // Из скрипта
        CHECK(runInt("let n = scene.get(0)\n n.stopAnimation()\n n.animationTime = 2.0\n if n.isAnimating() { return 1 }\n return 0") == 0);
        CHECK_NEAR(n.localPosition.x, 4.0, 1e-5);
        a = NodeAnimation();
    }

    TEST_SECTION("game: particles");
    resetScene();
    particles::clear();
    {
        int e = createNode(NODE_EMPTY, "Fire");
        sceneGraph[e].localPosition = vec3(0, 50, 0);          // Высоко над сценой
        ParticleEmitter& em = sceneGraph[e].emitter;
        em.enabled = true; em.rate = 100.0f; em.lifetime = 1.0f; em.speed = 2.0f; em.spread = 0.2f; em.gravityScale = 0.0f;
        updateSceneCaches();
        for (int i = 0; i < 30; i++) particles::update(1.0f / 60.0f, vec3(0, -9.8f, 0));
        CHECK(particles::pool.size() >= 45 && particles::pool.size() <= 55);   // 100 в секунду за полсекунды
        bool upward = true;
        for (const auto& p : particles::pool) if (p.velocity.y < 1.0f || p.position.y < 50.0f) upward = false;
        CHECK(upward);                                         // Конус вокруг +Y узла
        // Поток выключен — старые частицы доживают и исчезают
        CHECK(runInt("scene.find(\"Fire\").emitting = false\n return 1") == 1);
        for (int i = 0; i < 120; i++) particles::update(1.0f / 60.0f, vec3(0, -9.8f, 0));
        CHECK(particles::pool.empty());
        // Залп по команде и общий предел
        CHECK(runInt("scene.find(\"Fire\").emit(25)\n return 1") == 1);
        CHECK(particles::pool.size() == 25);
        particles::emit(e, 1000000);
        CHECK((int)particles::pool.size() == particles::MAX_PARTICLES);
        particles::clear();

        // Столкновение: частицы, летящие вниз, не проваливаются под пол демо-сцены
        sceneGraph[e].localPosition = vec3(0, 1.0f, 0);
        sceneGraph[e].localRotation = vec3(3.14159265f, 0, 0);  // +Y узла смотрит вниз
        em.collide = true; em.spread = 0.05f; em.speed = 3.0f; em.lifetime = 2.0f;
        updateSceneCaches();
        float ground = 1.0f;
        for (float y = 1.0f; y > -5.0f; y -= 0.01f) if (sceneDistance(vec3(0, y, 0)) < 0.0f) { ground = y; break; }
        if (ground < 1.0f) {
            particles::emit(e, 50);
            float lowest = 100.0f;
            for (int i = 0; i < 90; i++) { particles::update(1.0f / 60.0f, vec3(0, -9.8f, 0)); for (const auto& p : particles::pool) lowest = std::min(lowest, p.position.y); }
            CHECK(lowest > ground - 0.2f);
        }
        particles::clear();
    }

    TEST_SECTION("game: ui screens");
    resetScene();
    game::reset();
    {
        ui::Layout menu;
        ui::Element title; title.kind = ui::KIND_TEXT; title.name = "title"; title.text = "My | Game\nv1"; title.size = 48.0f;
        ui::Element playBtn; playBtn.kind = ui::KIND_BUTTON; playBtn.name = "play"; playBtn.text = "Play"; playBtn.x = 0.4f; playBtn.y = 0.5f; playBtn.w = 0.2f; playBtn.h = 0.1f;
        ui::Element cover = playBtn; cover.name = "cover"; cover.x = 0.45f; cover.w = 0.3f; cover.color = vec3(0.2f, 0.4f, 0.6f);
        ui::Element hp; hp.kind = ui::KIND_BAR; hp.name = "hp"; hp.value = 0.5f; hp.visible = false;
        menu.elements = { title, playBtn, cover, hp };
        CHECK(ui::save(dir + "/ui/menu.nlsui", menu));

        ui::Layout loaded;
        CHECK(ui::load(dir + "/ui/menu.nlsui", loaded));
        CHECK(loaded.elements.size() == 4);
        if (loaded.elements.size() == 4) {
            CHECK(loaded.elements[0].text == "My | Game\nv1");     // Разделитель и перенос строки в тексте сохраняются
            CHECK_NEAR(loaded.elements[0].size, 48.0, 1e-4);
            CHECK(loaded.elements[2].kind == ui::KIND_BUTTON && std::fabs(loaded.elements[2].color.z - 0.6f) < 1e-5f);
            CHECK(!loaded.elements[3].visible && std::fabs(loaded.elements[3].value - 0.5f) < 1e-6f);
        }

        std::string error;
        CHECK(runInt("ui.show(\"nope\")\n return 1", &error) == -99999 && error.find("not found") != std::string::npos);
        CHECK(runInt("if ui.clicked(\"menu\", \"play\") { return 1 }\n return 0") == 0);   // Экран не показан — не ошибка
        CHECK(runInt("ui.show(\"menu\")\n ui.show(\"menu\")\n if ui.isShown(\"menu\") { return 1 }\n return 0") == 1);
        CHECK(ui::shown.size() == 1);

        // Щелчок там, где кнопки перекрываются, достаётся верхней (поздней)
        ui::updateInput(0.5f, 0.55f, true, true);
        CHECK(runInt("if ui.clicked(\"menu\", \"play\") { return 1 }\n return 0") == 0);
        CHECK(runInt("if ui.clicked(\"menu\", \"cover\") { return 1 }\n return 0") == 1);
        ui::updateInput(0.42f, 0.55f, true, true);
        CHECK(runInt("if ui.clicked(\"menu\", \"play\") { return 1 }\n return 0") == 1);
        ui::updateInput(0.42f, 0.55f, true, false);               // Кнопка удерживается — щелчка уже нет
        CHECK(runInt("if ui.clicked(\"menu\", \"play\") { return 1 }\n return 0") == 0);

        CHECK(runInt("ui.setText(\"menu\", \"title\", \"Score 5\")\n ui.setValue(\"menu\", \"hp\", 0.25)\n ui.setVisible(\"menu\", \"hp\", true)\n ui.setVisible(\"menu\", \"cover\", false)\n return 1") == 1);
        std::vector<game::HudItem> items;
        if (!ui::shown.empty()) ui::appendItems(ui::shown[0], items);
        CHECK(items.size() == 3);
        if (items.size() == 3) {
            CHECK(items[0].kind == game::HUD_TEXT && items[0].text == "Score 5");
            CHECK(items[2].kind == game::HUD_BAR && std::fabs(items[2].value - 0.25f) < 1e-6f);
        }
        // Скрытая кнопка не перехватывает щелчок
        ui::updateInput(0.5f, 0.55f, true, true);
        CHECK(runInt("if ui.clicked(\"menu\", \"play\") { return 1 }\n return 0") == 1);
        CHECK(runInt("ui.setText(\"menu\", \"oops\", \"x\")\n return 1", &error) == -99999 && error.find("no element 'oops'") != std::string::npos);
        CHECK(runInt("ui.hide(\"menu\")\n if ui.isShown(\"menu\") { return 1 }\n return 0") == 0);

        CHECK(runInt("input.showCursor(true)\n return 1") == 1 && game::state.cursorVisible == 1);
        game::reset();
        CHECK(game::state.cursorVisible == -1 && ui::shown.empty());
    }

    TEST_SECTION("game: messages between scripts");
    resetScene();
    game::reset();
    {
        std::filesystem::create_directories(dir + "/scripts");
        std::ofstream(dir + "/scripts/sender.nls") <<
            "var target: node\n"
            "var sent = 0\n"
            "on update(dt: float) {\n"
            "    if sent == 0 { target.send(\"damage\", 30.0)\n target.send(\"heal\", 5.0)\n scene.broadcast(\"ping\", 1.0)\n sent = 1 }\n"
            "}\n"
            "on message(name: string, value: float) { if name == \"pong\" { self.position.x = value } }\n";
        std::ofstream(dir + "/scripts/receiver.nls") <<
            "var hp: float = 100.0\n"
            "var started = 0\n"
            "on start() { started = 1 }\n"
            "on message(name: string, value: float) {\n"
            "    if started == 0 { hp = -1000.0 }\n"
            "    if name == \"damage\" { hp -= value }\n"
            "    if name == \"heal\" { hp += value }\n"
            "    if name == \"ping\" { scene.find(\"Sphere\").send(\"pong\", 42.0) }\n"
            "    self.position.y = hp\n"
            "}\n";
        std::string savedRoot = scripts.rootDir;
        scripts.rootDir = dir;
        scripts.console.clear();
        sceneGraph[0].script.path = "scripts/sender.nls";
        sceneGraph[1].script.path = "scripts/receiver.nls";
        CHECK(scripts.syncVars(sceneGraph[0]));
        if (!sceneGraph[0].script.vars.empty()) sceneGraph[0].script.vars[0].node = 1;
        updateSceneCaches();
        scripts.start();
        scripts.update(1.0f / 60.0f);
        // Оба сообщения и ответ на рассылку дошли в том же кадре, start() получателя — раньше первого сообщения
        CHECK_NEAR(sceneGraph[1].localPosition.y, 75.0, 1e-4);
        CHECK_NEAR(sceneGraph[0].localPosition.x, 42.0, 1e-4);
        for (const auto& line : scripts.console) std::printf("  console: %s\n", line.c_str());
        CHECK(scripts.console.empty());

        // Получатель удалён, а место занял другой объект: письмо старому адресату не доходит
        scripts.send(1, "damage", 50.0f);
        destroyNode(1);
        flushDestroyed();
        SceneNode fresh = sceneGraph[0];
        fresh.name = "Fresh"; fresh.script.path = "scripts/receiver.nls"; fresh.script.vars.clear(); fresh.localPosition = vec3(0.0f);
        int slot = spawnNodes({ fresh });
        CHECK(slot == 1);
        updateSceneCaches();
        scripts.update(1.0f / 60.0f);
        CHECK_NEAR(sceneGraph[1].localPosition.y, 0.0, 1e-6);
        scripts.stop();
        scripts.rootDir = savedRoot;
    }

    TEST_SECTION("game: pathfinding");
    resetScene();
    game::reset();
    {
        // Пол (верх на y = -0.9), куб-препятствие на пути, агент — маленькая сфера
        sceneGraph[0].localScale = vec3(0.3f);
        sceneGraph[0].localPosition = vec3(-3.0f, -0.6f, 0.0f);
        sceneGraph[1].localPosition = vec3(2.0f, 0.0f, 0.0f);
        updateSceneCaches();

        nav::Query q;
        q.from = vec3(-3.0f, -0.6f, 0.0f); q.radius = 0.3f; q.height = 0.6f; q.ignoreNode = 0;
        std::vector<vec3> path;
        bool reached = false;

        // Открытый пол: одна прямая
        q.to = vec3(-3.0f, -0.6f, 4.0f);
        CHECK(nav::findPath(q, path, &reached) && reached && path.size() == 1);

        // Цель за кубом: путь в обход, длиннее прямой, ни одна точка не касается куба
        q.to = vec3(6.0f, -0.6f, 0.0f);
        CHECK(nav::findPath(q, path, &reached) && reached);
        CHECK(path.size() >= 2 && path.size() <= 6);                 // Спрямлён, а не по клеткам
        float length = nav::pathLength(q.from, path);
        CHECK(length > 9.2f && length < 12.0f);
        if (!path.empty()) CHECK_NEAR(glm::distance(path.back(), q.to), 0.0, 1e-4);
        bool clear = true;
        vec3 from = q.from;
        for (const vec3& p : path) {
            for (int i = 0; i <= 20; i++) {
                vec3 at = glm::mix(from, p, i / 20.0f);
                vec3 d = glm::abs(at - vec3(2.0f, 0.0f, 0.0f)) - vec3(1.0f);
                if (std::max(d.x, d.z) < 0.25f) clear = false;       // Ближе 0.25 к стенкам куба
            }
            from = p;
        }
        CHECK(clear);

        // Цель внутри куба и цель за краем пола: путь есть, но до цели не доходит
        q.to = vec3(2.0f, -0.6f, 0.0f);
        CHECK(nav::findPath(q, path, &reached) && !reached && !path.empty());
        if (!path.empty()) CHECK(glm::distance(path.back(), q.to) < 2.2f);
        q.to = vec3(30.0f, -0.6f, 0.0f);
        CHECK(nav::findPath(q, path, &reached) && !reached);
        if (!path.empty()) CHECK(path.back().x < 8.3f);              // Не дальше края пола

        // Из скрипта: объект сам доходит до цели и по дороге не входит в куб
        CHECK(runInt("let n = scene.get(0)\n n.navigateTo(vec3(6, -0.6, 0), 3.0)\n if n.isNavigating() { return 1 }\n return 0") == 1);
        float closest = 100.0f;
        int frames = 0;
        for (; frames < 900 && game::activeAgents() > 0; frames++) {
            updateSceneCaches();
            game::updateNavigation(1.0f / 60.0f);
            vec3 d = glm::abs(sceneGraph[0].localPosition - vec3(2.0f, 0.0f, 0.0f)) - vec3(1.0f);
            closest = std::min(closest, std::max(d.x, d.z));
        }
        CHECK(game::activeAgents() == 0);
        CHECK_NEAR(sceneGraph[0].localPosition.x, 6.0, 0.02);
        CHECK_NEAR(sceneGraph[0].localPosition.y, -0.6, 1e-4);        // Высота не меняется
        CHECK(closest > 0.25f);
        CHECK(frames > 180 && frames < 260);                          // ~10 м со скоростью 3 м/с
        updateSceneCaches();
        float around = -1.0f;
        { Program program; std::vector<CompileError> errors; VM vm; Value v;
          if (compile("func test() -> float { return scene.get(0).pathDistance(vec3(-3, -0.6, 0)) }", program, errors)) { vm.init(&program); if (vm.call(program.findFunction("test", false), nullptr, 0, &v)) around = v.f; } }
        CHECK(around > 9.2f && around < 12.0f);
    }

    TEST_SECTION("game: input actions and mixer");
    game::reset();
    {
        std::string file = dir + "/actions.dat";
        game::setSaveFile(file);
        auto savedDown = host.keyDown; auto savedPressed = host.keyPressed; auto savedLast = host.lastKey;
        static std::string held;
        host.keyDown = [](const std::string& key) { return key == held; };
        host.keyPressed = [](const std::string& key) { return key == held; };
        host.lastKey = []() { return held; };
        held = "space";
        CHECK(runInt("if input.action(\"jump\") and input.actionPressed(\"jump\") and not input.action(\"fire\") { return 1 }\n return 0") == 1);
        // Переназначение: старая клавиша больше не работает, новая — работает
        CHECK(runInt("input.bind(\"jump\", input.lastKey())\n input.bind(\"jump\", \"k\")\n if input.action(\"jump\") { return 1 }\n return 0") == 0);
        held = "k";
        CHECK(runInt("if input.action(\"jump\") { return 1 }\n return 0") == 1);
        std::string error;
        CHECK(runInt("if input.action(\"dance\") { return 1 }\n return 0", &error) == -99999 && error.find("unknown input action") != std::string::npos);
        CHECK(runInt("input.define(\"dance\", \"k\")\n input.define(\"jump\", \"z\")\n if input.action(\"dance\") and input.action(\"jump\") { return 1 }\n return 0") == 1);
        // Переназначение переживает перезапуск, неназначенное возвращается к умолчанию
        CHECK(game::flushSave());
        game::setSaveFile(file);
        CHECK(game::actionKey("jump") == "k" && game::actionKey("forward") == "w" && game::actionKey("dance").empty());
        game::setSaveFile("");
        CHECK(game::actionKey("jump") == "space");
        host.keyDown = savedDown; host.keyPressed = savedPressed; host.lastKey = savedLast;

        // Громкости групп запоминаются и без звукового устройства, значения обрезаются
        CHECK_NEAR((double)runInt("audio.setMusicVolume(0.25)\n audio.setSoundVolume(7.0)\n return int(audio.musicVolume() * 100.0 + audio.soundVolume() * 1000.0)"), 2025.0, 1.0);
        audio::setMusicVolume(1.0f); audio::setSfxVolume(1.0f);
        CHECK(audio::currentMusic().empty());
    }

    host.resolvePath = savedResolve;
    std::filesystem::remove_all(dir);
    resetScene();
}
