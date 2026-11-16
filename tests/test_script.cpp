#include "TestUtil.h"
#include "ScriptSystem.h"
#include "GameApi.h"
#include "Scene.h"
#include "Physics.h"
#include <filesystem>
#include <fstream>

using namespace nls;

static std::string lastLog;
static bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Собирает скрипт и вызывает функцию test(); результат нужного типа
static bool run(const std::string& source, Value& result, std::string* error = nullptr) {
    Program program;
    std::vector<CompileError> errors;
    if (!compile(source, program, errors)) {
        if (error) *error = errors.empty() ? "?" : errors[0].message;
        else for (const auto& e : errors) std::printf("  compile error %d:%d: %s\n", e.line, e.column, e.message.c_str());
        return false;
    }
    VM vm;
    vm.init(&program);
    int fn = program.findFunction("test", false);
    if (fn < 0) { if (error) *error = "no test()"; return false; }
    bool ok = vm.call(fn, nullptr, 0, &result);
    if (!ok && error) *error = vm.error;
    if (ok && program.functions[fn].ret == Type::String) lastLog = vm.string(result);
    return ok;
}

static float evalFloat(const std::string& body) { Value v; return run("func test() -> float { " + body + " }", v) ? v.f : NAN; }
static int evalInt(const std::string& body) { Value v; return run("func test() -> int { " + body + " }", v) ? v.i : -99999; }
static bool evalBool(const std::string& body) { Value v; return run("func test() -> bool { " + body + " }", v) && v.i != 0; }
static std::string evalString(const std::string& body) { Value v; lastLog.clear(); run("func test() -> string { " + body + " }", v); return lastLog; }

static std::string compileError(const std::string& source, int* line = nullptr) {
    Program program;
    std::vector<CompileError> errors;
    if (compile(source, program, errors)) return "";
    if (line) *line = errors[0].line;
    return errors[0].message;
}


void runScriptTests() {
    registerEngineBindings();
    spdlog::set_level(spdlog::level::warn);

    TEST_SECTION("script: expressions");
    CHECK_NEAR(evalFloat("return 1.5 + 2.0 * 3.0"), 7.5, 1e-6);
    CHECK_NEAR(evalFloat("return (1.5 + 2.0) * 3.0"), 10.5, 1e-6);
    CHECK_NEAR(evalFloat("return 7 / 2"), 3.0, 1e-6);            // целочисленное деление, затем int -> float
    CHECK_NEAR(evalFloat("return 7 / 2.0"), 3.5, 1e-6);
    CHECK_NEAR(evalFloat("return 2 * 1.5"), 3.0, 1e-6);
    CHECK_NEAR(evalFloat("return -3.0 + 1"), -2.0, 1e-6);
    CHECK(evalInt("return 17 % 5") == 2);
    CHECK(evalInt("return int(3.9)") == 3);
    CHECK(evalInt("return -(2 + 3) * 4") == -20);
    CHECK(evalBool("return 3 > 2 and 2.5 <= 2.5 and not (1 == 2)"));
    CHECK(evalBool("return 1 > 2 or 2 != 3"));
    CHECK(!evalBool("return true and false"));
    CHECK_NEAR(evalFloat("let v = vec3(1, 2, 3) * 2.0 + vec3(1, 1, 1)\n return v.x + v.y + v.z"), 15.0, 1e-6);
    CHECK_NEAR(evalFloat("return length(vec3(3, 4, 0))"), 5.0, 1e-5);
    CHECK_NEAR(evalFloat("return dot(cross(vec3(1,0,0), vec3(0,1,0)), vec3(0,0,1))"), 1.0, 1e-6);
    CHECK_NEAR(evalFloat("return clamp(5, 0, 2) + lerp(0, 10, 0.5) + max(1, 3)"), 10.0, 1e-6);
    CHECK(evalString("return \"a\" + 1 + \" \" + 2.5 + \" \" + true + \" \" + vec3(1, 2, 3)") == "a1 2.5 true (1, 2, 3)");
    CHECK(evalString("return 1 + \"b\"") == "1b");
    CHECK(evalBool("return \"abc\" == \"ab\" + \"c\""));

    TEST_SECTION("script: control flow and functions");
    CHECK(evalInt("let s = 0\n for i in 0..10 { s += i }\n return s") == 45);
    CHECK(evalInt("let s = 0\n let i = 0\n while true { i += 1\n if i > 100 { break }\n if i % 2 == 0 { continue }\n s += i }\n return s") == 2500);
    CHECK(evalInt("let x = 5\n if x > 10 { return 1 } else if x > 3 { return 2 } else { return 3 }") == 2);
    CHECK(evalInt("let v = vec3(1, 2, 3)\n v.y = 10\n v.z *= 2\n return int(v.x + v.y + v.z)") == 17);
    {
        Value v;
        CHECK(run("func fib(n: int) -> int { if n < 2 { return n }\n return fib(n - 1) + fib(n - 2) }\nfunc test() -> int { return fib(20) }", v));
        CHECK(v.i == 6765);
        // Функция объявлена ниже места вызова; int-аргумент приводится к float
        CHECK(run("func test() -> float { return half(9) }\nfunc half(x: float) -> float { return x / 2 }", v));
        CHECK_NEAR(v.f, 4.5, 1e-6);
        // Глобальные переменные и их инициализаторы
        CHECK(run("var a = 2\nvar b: float = a * 1.5\nvar name = \"n\" + a\nfunc test() -> float { a += 1\n return b + a }", v));
        CHECK_NEAR(v.f, 6.0, 1e-6);
        // Перенос строки перед минусом начинает новый оператор
        std::string error;
        CHECK(run("func test() -> int {\n let a = 5\n let b = a\n  - 2\n return b }", v, &error) == false);
        CHECK(contains(error, "does nothing"));
    }

    TEST_SECTION("script: compile errors");
    int line = 0;
    CHECK(contains(compileError("func test() { let x: int = 1.5 }"), "expected int, got float"));
    CHECK(contains(compileError("func test() {\n\n  y = 3\n}", &line), "unknown variable 'y'"));
    CHECK(line == 3);
    CHECK(contains(compileError("func test() { foo(1) }"), "unknown function 'foo'"));
    CHECK(contains(compileError("func test() { log(1, 2) }"), "takes 1 argument"));
    CHECK(contains(compileError("func test() -> float { return \"s\" }"), "expected float, got string"));
    CHECK(contains(compileError("func test() { if 1 { } }"), "condition must be bool"));
    CHECK(contains(compileError("on explode() { }"), "unknown event"));
    CHECK(contains(compileError("on update() { }"), "takes (float)"));
    CHECK(contains(compileError("func test() { self.nope = 1 }"), "no property 'nope'"));
    CHECK(contains(compileError("func test() { self.name = \"x\" }"), "read-only"));
    CHECK(contains(compileError("func test() { let a = 1 + }"), "unexpected"));
    CHECK(contains(compileError("func test() { break }"), "outside of a loop"));
    CHECK(contains(compileError("func test() { let v = vec3(1,2,3)\n v.w = 1 }"), "x, y, z"));
    CHECK(contains(compileError("func test() { 1 + 2 }"), "does nothing"));
    CHECK(contains(compileError("func test() { let s = \"abc }"), "unterminated string"));
    CHECK(contains(compileError("func test() { input.nope() }"), "no function 'nope'"));
    {
        // Несколько ошибок за один проход: по одной на функцию
        Program program;
        std::vector<CompileError> errors;
        CHECK(!compile("func a() { x = 1 }\nfunc b() { y = 2 }\n", program, errors));
        CHECK(errors.size() == 2);
    }

    TEST_SECTION("script: runtime safety");
    {
        Value v;
        std::string error;
        CHECK(!run("func test() -> int { while true { } return 1 }", v, &error));
        CHECK(contains(error, "ran too long"));
        CHECK(!run("func test() -> int { return test() }", v, &error));
        CHECK(contains(error, "stack overflow"));
        CHECK(!run("func test() -> int { let z = 0\n return 5 / z }", v, &error));
        CHECK(contains(error, "line 2") && contains(error, "division by zero"));
        CHECK(!run("func test() -> float { let n: node\n return n.position.x }", v, &error));
        CHECK(contains(error, "null node"));
    }

    TEST_SECTION("script: engine bindings");
    resetScene();
    updateSceneCaches();
    {
        Program program;
        std::vector<CompileError> errors;
        const char* source =
            "var speed: float = 2.0\n"
            "var target: node\n"
            "var label = \"hi\"\n"
            "var hits = 0\n"
            "on start() { target = scene.find(\"Cube\")\n self.position.y += speed }\n"
            "on update(dt: float) {\n"
            "    self.rotation.y += 90 * dt\n"
            "    target.position = target.position + vec3(1, 0, 0) * dt\n"
            "    target.body.velocity = vec3(0, 3, 0)\n"
            "    if target.name == \"Cube\" { target.color = vec3(1, 0, 0) }\n"
            "}\n"
            "on collide(other: node) { hits += 1 }\n";
        CHECK(compile(source, program, errors));
        for (const auto& e : errors) std::printf("  %d:%d %s\n", e.line, e.column, e.message.c_str());
        VM vm;
        vm.init(&program);
        vm.self = 0;
        CHECK(program.globals.size() == 4);
        CHECK_NEAR(vm.globals[0].f, 2.0, 1e-6);
        CHECK(vm.globals[1].i == -1);
        CHECK(vm.string(vm.globals[2]) == "hi");
        CHECK(vm.call(program.findFunction("start", true)));
        CHECK(vm.globals[1].i == 1);
        CHECK_NEAR(sceneGraph[0].localPosition.y, 2.5, 1e-5);
        Value dt = Value::Float(0.5f);
        CHECK(vm.call(program.findFunction("update", true), &dt, 1));
        CHECK_NEAR(glm::degrees(sceneGraph[0].localRotation.y), 45.0, 1e-3);
        CHECK_NEAR(sceneGraph[1].localPosition.x, 2.5, 1e-5);
        CHECK_NEAR(sceneGraph[1].body.velocity.y, 3.0, 1e-6);
        CHECK_NEAR(sceneGraph[1].gpuData.color.x, 1.0, 1e-6);
        CHECK_NEAR(sceneGraph[1].gpuData.color.y, 0.0, 1e-6);
        CHECK(vm.string(vm.globals[2]) == "hi");   // Строка в переменной пережила чистку временных

        // .nlsc: байткод сохраняется, загружается и работает так же
        std::string path = (std::filesystem::temp_directory_path() / "nlsdfe_test.nlsc").string();
        CHECK(saveProgram(program, path));
        Program loaded;
        CHECK(loadProgram(path, loaded));
        CHECK(loaded.code == program.code);
        VM vm2;
        vm2.init(&loaded);
        vm2.self = 0;
        CHECK(vm2.call(loaded.findFunction("start", true)));
        float before = sceneGraph[0].localRotation.y;
        CHECK(vm2.call(loaded.findFunction("update", true), &dt, 1));
        CHECK_NEAR(glm::degrees(sceneGraph[0].localRotation.y - before), 45.0, 1e-3);
        std::filesystem::remove(path);
    }

    TEST_SECTION("script: system with physics");
    resetScene();
    physics::reset();
    {
        std::string dir = (std::filesystem::temp_directory_path() / "nlsdfe_scripts").string();
        std::filesystem::create_directories(dir);
        std::ofstream(dir + "/ball.nls") <<
            "var bounces = 0\n"
            "var kick: float = 0.0\n"
            "on start() { self.velocity = vec3(0, kick, 0) }\n"
            "on collide(other: node) { bounces += 1\n if other.name == \"Floor\" { self.color = vec3(0, 1, 0) } }\n"
            "on fixedUpdate(dt: float) { self.angularVelocity = vec3(0, 1, 0) }\n";
        std::ofstream(dir + "/broken.nls") << "on update(dt: float) { oops = 1 }\n";
        scripts.rootDir = dir;
        scripts.console.clear();

        SceneNode ball = sceneGraph[0];
        ball.name = "Ball";
        ball.localPosition = {5, 2, 5};
        ball.localScale = vec3(0.3f);
        ball.body.type = BODY_DYNAMIC;
        ball.script.path = "ball.nls";
        sceneGraph.push_back(ball);
        int ballIdx = (int)sceneGraph.size() - 1;

        // Инспектор: переменные скрипта подтягиваются со значениями по умолчанию и сохраняют правки
        CHECK(scripts.syncVars(sceneGraph[ballIdx]));
        CHECK(sceneGraph[ballIdx].script.vars.size() == 2);
        if (sceneGraph[ballIdx].script.vars.size() == 2) {
            CHECK(sceneGraph[ballIdx].script.vars[1].name == "kick");
            sceneGraph[ballIdx].script.vars[1].value.x = 4.0f;
            CHECK(scripts.syncVars(sceneGraph[ballIdx]));
            CHECK_NEAR(sceneGraph[ballIdx].script.vars[1].value.x, 4.0, 1e-6);
        }
        sceneGraph[1].script.path = "broken.nls";
        std::vector<CompileError> errors;
        CHECK(!scripts.syncVars(sceneGraph[1], &errors));
        CHECK(errors.size() == 1 && contains(errors[0].message, "unknown variable 'oops'"));

        updateSceneCaches();
        scripts.start();
        physics::Events events;
        events.onCollide = [](int a, int b, vec3, vec3, float) { scripts.onCollide(a, b); };
        float maxY = -100.0f;
        for (int frame = 0; frame < 240; frame++) {
            updateSceneCaches();
            scripts.update(1.0f / 60.0f);
            physics::step(1.0f / 60.0f, {}, events, [](float dt) { scripts.fixedUpdate(dt); });
            maxY = std::max(maxY, sceneGraph[ballIdx].localPosition.y);
        }
        CHECK(maxY > 2.2f);                                              // start() подбросил мяч вверх значением из инспектора
        CHECK_NEAR(sceneGraph[ballIdx].gpuData.color.y, 1.0, 1e-6);      // collide() с полом сработал
        CHECK_NEAR(sceneGraph[ballIdx].body.angularVelocity.y, 1.0, 0.2);// fixedUpdate() крутит тело
        bool reportedBroken = false;
        for (const auto& l : scripts.console) if (contains(l, "broken.nls") && contains(l, "oops")) reportedBroken = true;
        CHECK(reportedBroken);                                           // Битый скрипт не уронил остальные
        scripts.stop();
        scripts.rootDir.clear();
        std::filesystem::remove_all(dir);
    }

    TEST_SECTION("script: game services");
    resetScene();
    updateSceneCaches();
    game::reset();
    {
        // Сохранения переживают перезапуск: запись, сброс на диск, чтение заново
        std::string file = (std::filesystem::temp_directory_path() / "nlsdfe_save_test.dat").string();
        std::filesystem::remove(file);
        game::setSaveFile(file);
        CHECK_NEAR(evalFloat("return save.getFloat(\"best\", 7.5)"), 7.5, 1e-6);
        CHECK(evalBool("save.setFloat(\"best\", 12.25)\n save.setInt(\"level\", 3)\n save.setString(\"name\", \"a=b\\\\c\")\n return save.has(\"level\")"));
        CHECK(game::flushSave());
        game::setSaveFile(file);
        CHECK_NEAR(evalFloat("return save.getFloat(\"best\", 0.0)"), 12.25, 1e-6);
        CHECK(evalInt("return save.getInt(\"level\", 0)") == 3);
        CHECK(evalString("return save.getString(\"name\", \"\")") == "a=b\\c");
        CHECK(!evalBool("save.remove(\"level\")\n return save.has(\"level\")"));
        CHECK(evalInt("save.clear()\n return save.getInt(\"level\", -1)") == -1);
        std::filesystem::remove(file);
        game::setSaveFile("");

        // Переходы: середина пути на половине времени, точно в цели в конце
        sceneGraph[0].localPosition = vec3(0.0f);
        CHECK(evalBool("scene.get(0).moveTo(vec3(2, 4, 6), 1.0)\n return scene.get(0).isMoving()"));
        game::updateTweens(0.5f);
        CHECK_NEAR(sceneGraph[0].localPosition.y, 2.0, 1e-4);
        game::updateTweens(0.6f);
        CHECK_NEAR(sceneGraph[0].localPosition.z, 6.0, 1e-5);
        CHECK(game::activeTweens() == 0);
        // Новый переход того же свойства заменяет старый; нулевая длительность — мгновенно
        evalBool("scene.get(0).moveTo(vec3(9, 9, 9), 5.0)\n scene.get(0).moveTo(vec3(1, 1, 1), 0.0)\n scene.get(0).scaleTo(vec3(2, 2, 2), 1.0)\n return true");
        CHECK(game::activeTweens() == 1);
        CHECK_NEAR(sceneGraph[0].localPosition.x, 1.0, 1e-6);
        game::updateTweens(2.0f);
        CHECK_NEAR(sceneGraph[0].localScale.x, 2.0, 1e-5);

        // lookAt и forward согласованы
        sceneGraph[0].localPosition = vec3(0.0f);
        sceneGraph[0].localScale = vec3(1.0f);
        updateSceneCaches();
        CHECK_NEAR(evalFloat("scene.get(0).lookAt(vec3(3, 0, 0))\n return scene.get(0).forward().x"), 1.0, 1e-4);
        CHECK_NEAR(evalFloat("scene.get(0).lookAt(vec3(0, 5, -5))\n return scene.get(0).forward().y"), std::sqrt(0.5), 1e-4);

        // Интерфейс: элементы копятся за кадр, кнопка срабатывает под курсором в кадр щелчка
        game::beginFrame();
        game::state.mouseX = 0.5f; game::state.mouseY = 0.5f; game::state.mouseDown = true; game::state.mousePressed = true;
        CHECK(evalBool("ui.text(\"Score\", 0.1, 0.1, 24.0, vec3(1, 1, 1))\n ui.bar(0.1, 0.2, 0.3, 0.05, 1.7, vec3(1, 0, 0))\n return ui.button(\"Play\", 0.4, 0.4, 0.2, 0.2)"));
        CHECK(game::state.hud.size() == 3);
        if (game::state.hud.size() == 3) {
            CHECK(game::state.hud[0].text == "Score");
            CHECK_NEAR(game::state.hud[1].value, 1.0, 1e-6);      // Заполнение обрезается до 0..1
            CHECK(game::state.hud[2].text == "Play");
        }
        CHECK(!evalBool("return ui.button(\"Far\", 0.8, 0.8, 0.1, 0.1)"));
        game::state.mousePressed = false;
        CHECK(!evalBool("return ui.button(\"Play\", 0.4, 0.4, 0.2, 0.2)"));
        game::beginFrame();
        CHECK(game::state.hud.empty());

        // Оси ввода, таймер, запросы сцены
        auto savedKeyDown = host.keyDown;
        host.keyDown = [](const std::string& key) { return key == "d" || key == "s"; };
        CHECK_NEAR(evalFloat("return input.axis(\"horizontal\")"), 1.0, 1e-6);
        CHECK_NEAR(evalFloat("return input.axis(\"vertical\")"), -1.0, 1e-6);
        host.keyDown = savedKeyDown;
        std::string error;
        Value v;
        CHECK(!run("func test() -> float { return input.axis(\"jump\") }", v, &error) && contains(error, "unknown axis"));

        host.time = 2.01f; host.delta = 0.02f;
        CHECK(evalBool("return time.every(1.0)"));
        host.time = 2.5f;
        CHECK(!evalBool("return time.every(1.0)"));
        host.time = 0.0f; host.delta = 0.0f;

        CHECK_NEAR(evalFloat("time.setScale(0.25)\n return time.scale()"), 0.25, 1e-6);
        evalBool("scene.load(\"level2\")\n game.quit()\n return true");
        CHECK(game::state.pendingScene == "level2" && game::state.quitRequested);
        // Пример из README собирается как есть
        CHECK(compileError(
            "var hp: float = 1.0\n"
            "on update(dt: float) {\n"
            "    ui.bar(0.03, 0.04, 0.25, 0.03, hp, vec3(0.9, 0.2, 0.2))\n"
            "    ui.text(\"Best: \" + str(save.getInt(\"best\", 0)), 0.03, 0.09, 22.0, vec3(1, 1, 1))\n"
            "    if hp <= 0.0 and ui.button(\"Restart\", 0.4, 0.45, 0.2, 0.08) { scene.restart() }\n"
            "}\n") == "");
        game::reset();
        CHECK(game::state.pendingScene.empty() && !game::state.quitRequested && game::state.timeScale == 1.0f);
    }

    spdlog::set_level(spdlog::level::info);
    resetScene();
}
