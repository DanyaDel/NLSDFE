#include "TestUtil.h"
#include "VisualScript.h"
#include "ScriptSystem.h"
#include "Scene.h"
#include <filesystem>

using namespace nls;
using namespace nls::visual;

static bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void runVisualTests() {
    registerEngineBindings();
    spdlog::set_level(spdlog::level::warn);

    TEST_SECTION("visual: catalog follows the registry");
    {
        Graph g;
        g.vars.push_back({"speed", Type::Float, "90"});
        std::vector<NodeDef> defs = catalog(g);
        CHECK(findDef(defs, "event.update") != nullptr);
        CHECK(findDef(defs, "call.log") != nullptr && findDef(defs, "call.log")->kind == NodeKind::Call);
        CHECK(findDef(defs, "call.sin") != nullptr && findDef(defs, "call.sin")->kind == NodeKind::Pure);
        CHECK(findDef(defs, "call.node.applyImpulse") != nullptr);
        CHECK(findDef(defs, "set.position") != nullptr);
        CHECK(findDef(defs, "set.name") == nullptr);          // Только для чтения — узла записи нет
        CHECK(findDef(defs, "var.get.speed") != nullptr);
        // Новая функция в реестре сразу становится узлом
        registry().function("test.answer", Type::Int, {}, [](VM&, Value*, Value& r) { r = Value::Int(42); });
        CHECK(findDef(catalog(g), "call.test.answer") != nullptr);
    }

    TEST_SECTION("visual: graph compiles and runs");
    resetScene();
    updateSceneCaches();
    {
        // On Update: self.rotation = rotation + (0, speed * dt, 0); если speed > 50 — log
        Graph g;
        g.vars.push_back({"speed", Type::Float, "90"});
        int update = g.addNode("event.update", 0, 0);
        int set = g.addNode("set.rotation", 300, 0);
        int get = g.addNode("get.rotation", 0, 150);
        int speed = g.addNode("var.get.speed", 0, 250);
        int mul = g.addNode("op.mul_f", 150, 250);
        int make = g.addNode("op.make_v", 250, 250);
        int add = g.addNode("op.add_v", 350, 150);
        int branch = g.addNode("flow.branch", 500, 0);
        int gt = g.addNode("op.gt", 400, 300);
        int log = g.addNode("call.log", 700, 0);
        CHECK(update && set && get && speed && mul && make && add && branch && gt && log);

        CHECK(g.connect(update, 0, set, 0));          // exec
        CHECK(g.connect(speed, 0, mul, 0));
        CHECK(g.connect(update, 1, mul, 1));          // dt
        CHECK(g.connect(mul, 0, make, 1));            // Y
        CHECK(g.connect(get, 0, add, 0));
        CHECK(g.connect(make, 0, add, 1));
        CHECK(g.connect(add, 0, set, 2));             // Value
        CHECK(g.connect(set, 0, branch, 0));
        CHECK(g.connect(speed, 0, gt, 0));
        g.find(gt)->constants[1] = "50";
        CHECK(g.connect(gt, 0, branch, 1));
        CHECK(g.connect(branch, 0, log, 0));
        g.find(log)->constants[1] = "fast \"spinner\"";

        // Проверка типов при соединении
        std::string why;
        CHECK(!g.connect(update, 0, mul, 0, &why) && has(why, "execution"));
        CHECK(!g.connect(get, 0, mul, 0, &why) && has(why, "types"));
        CHECK(!g.connect(mul, 0, mul, 1, &why));

        Generated gen = generate(g);
        CHECK(gen.errors.empty());
        CHECK(has(gen.source, "var speed: float = 90.0"));
        CHECK(has(gen.source, "on update(dt: float) {"));
        CHECK(has(gen.source, "log(\"fast \\\"spinner\\\"\")"));

        Program program;
        std::vector<CompileError> errors;
        CHECK(compileGraph(g, program, errors));
        for (const auto& e : errors) std::printf("  %s\n%s\n", e.message.c_str(), gen.source.c_str());

        VM vm;
        vm.init(&program);
        vm.self = 0;
        scripts.console.clear();
        Value dt = Value::Float(0.5f);
        CHECK(vm.call(program.findFunction("update", true), &dt, 1));
        CHECK_NEAR(glm::degrees(sceneGraph[0].localRotation.y), 45.0, 1e-3);
        CHECK(!scripts.console.empty() && scripts.console.back() == "fast \"spinner\"");

        // Сохранение и загрузка дают тот же код
        std::string path = (std::filesystem::temp_directory_path() / "nlsdfe_test.nlsg").string();
        CHECK(saveGraph(g, path));
        Graph loaded;
        std::string error;
        CHECK(loadGraph(path, loaded, &error));
        CHECK(generate(loaded).source == gen.source);
        std::filesystem::remove(path);
    }

    TEST_SECTION("visual: for loop, sequence, impure call, merged events");
    {
        Graph g;
        g.vars.push_back({"total", Type::Int, "0"});
        g.vars.push_back({"spawned", Type::Node, ""});
        int start = g.addNode("event.start", 0, 0);
        int start2 = g.addNode("event.start", 0, 200);
        int seq = g.addNode("flow.sequence", 150, 0);
        int loop = g.addNode("flow.for", 300, 0);
        int setTotal = g.addNode("var.set.total", 450, 0);
        int getTotal = g.addNode("var.get.total", 300, 150);
        int addI = g.addNode("op.add_i", 400, 150);
        int spawn = g.addNode("call.scene.spawn", 300, 300);
        int setSpawned = g.addNode("var.set.spawned", 450, 300);
        int self = g.addNode("node.self", 150, 300);
        g.connect(start, 0, seq, 0);
        g.connect(seq, 0, loop, 0);
        g.find(loop)->constants[2] = "5";
        g.connect(loop, 0, setTotal, 0);
        g.connect(getTotal, 0, addI, 0);
        g.connect(loop, 1, addI, 1);
        g.connect(addI, 0, setTotal, 1);
        g.connect(start2, 0, spawn, 0);             // Второй узел On Start: тот же обработчик
        g.connect(spawn, 0, setSpawned, 0);
        g.connect(spawn, 1, setSpawned, 1);
        CHECK(g.connect(self, 0, spawn, 1));         // Шаблон — сам объект
        Program program;
        std::vector<CompileError> errors;
        CHECK(compileGraph(g, program, errors));
        for (const auto& e : errors) std::printf("  %s\n%s\n", e.message.c_str(), generate(g).source.c_str());
        VM vm;
        vm.init(&program);
        vm.self = 1;
        size_t before = sceneGraph.size();
        CHECK(vm.call(program.findFunction("start", true)));
        CHECK(vm.globals[0].i == 0 + 1 + 2 + 3 + 4);
        CHECK(sceneGraph.size() == before + 1);
        CHECK(vm.globals[1].i == (int)before);      // Ссылка на созданный узел сохранилась
    }

    TEST_SECTION("visual: errors point at nodes");
    {
        Graph g;
        int update = g.addNode("event.update", 0, 0);
        int a = g.addNode("flow.sequence", 100, 0);
        int b = g.addNode("flow.sequence", 200, 0);
        g.connect(update, 0, a, 0);
        g.connect(a, 0, b, 0);
        g.connect(b, 0, a, 0);                     // Поток замкнут в кольцо
        Program program;
        std::vector<CompileError> errors;
        std::vector<int> nodes;
        CHECK(!compileGraph(g, program, errors, &nodes));
        CHECK(!errors.empty() && has(errors[0].message, "loops back"));
        CHECK(!nodes.empty() && (nodes[0] == a || nodes[0] == b));

        // Ошибка компилятора NLS тоже приводится к узлу: результат scene.spawn берётся вне цепочки
        Graph g2;
        int start = g2.addNode("event.start", 0, 0);
        int spawn = g2.addNode("call.scene.spawn", 100, 100);    // Не подключён к потоку
        int log = g2.addNode("call.log", 200, 0);
        int str = g2.addNode("op.str_n", 150, 100);
        g2.connect(start, 0, log, 0);
        g2.connect(spawn, 1, str, 0);
        g2.connect(str, 0, log, 1);
        CHECK(!compileGraph(g2, program, errors, &nodes));
        CHECK(!nodes.empty() && nodes[0] == log);
        CHECK(!errors.empty() && has(errors[0].message, "node #") && has(errors[0].message, "r_"));

        // Недопустимое имя переменной
        Graph g3;
        g3.vars.push_back({"for", Type::Float, "0"});
        CHECK(!compileGraph(g3, program, errors));
        CHECK(!isIdentifier("input") && !isIdentifier("2x") && isIdentifier("doorSpeed"));
    }

    TEST_SECTION("visual: graph attached to a node through ScriptSystem");
    {
        std::string dir = (std::filesystem::temp_directory_path() / "nlsdfe_visual").string();
        std::filesystem::create_directories(dir);
        Graph g;
        g.vars.push_back({"lift", Type::Float, "2"});
        int start = g.addNode("event.start", 0, 0);
        int set = g.addNode("set.position", 200, 0);
        int lift = g.addNode("var.get.lift", 0, 100);
        int make = g.addNode("op.make_v", 100, 100);
        g.connect(start, 0, set, 0);
        g.connect(lift, 0, make, 1);
        g.connect(make, 0, set, 2);
        CHECK(saveGraph(g, dir + "/lift.nlsg"));

        resetScene();
        scripts.rootDir = dir;
        sceneGraph[1].script.path = "lift.nlsg";
        CHECK(scripts.syncVars(sceneGraph[1]));
        CHECK(sceneGraph[1].script.vars.size() == 1 && sceneGraph[1].script.vars[0].name == "lift");
        sceneGraph[1].script.vars[0].value.x = 7.0f;   // Как правка в инспекторе
        updateSceneCaches();
        scripts.start();
        scripts.update(1.0f / 60.0f);
        CHECK_NEAR(sceneGraph[1].localPosition.y, 7.0, 1e-5);
        scripts.stop();
        scripts.rootDir.clear();
        std::filesystem::remove_all(dir);
    }

    spdlog::set_level(spdlog::level::info);
    resetScene();
}
