#include "ScriptSystem.h"
#include "Scene.h"
#include "VisualScript.h"
#include <fstream>
#include <sstream>

namespace nls {

ScriptSystem scripts;

namespace fs = std::filesystem;

void ScriptSystem::print(const std::string& line) {
    spdlog::info("[script] {}", line);
    console.push_back(line);
    if (console.size() > 300) console.erase(console.begin(), console.begin() + 100);
}

std::string ScriptSystem::resolve(const std::string& path) const {
    fs::path p(path);
    if (p.is_absolute() || rootDir.empty()) return path;
    return (fs::path(rootDir) / p).string();
}

std::shared_ptr<const Program> ScriptSystem::load(const std::string& path, std::vector<CompileError>* errors) {
    if (errors) errors->clear();
    std::string full = resolve(path);
    std::error_code ec;

    if (!fs::exists(full, ec)) {
        // Экспортированная игра: исходников нет, рядом лежит байткод
        std::string compiled = fs::path(full).replace_extension(".nlsc").string();
        auto it = cache.find(compiled);
        if (it != cache.end()) return it->second.program;
        auto program = std::make_shared<Program>();
        if (!fs::exists(compiled, ec) || !loadProgram(compiled, *program)) {
            if (errors) errors->push_back({0, 0, "script file not found: " + full});
            return nullptr;
        }
        cache[compiled].program = program;
        return program;
    }

    auto time = fs::last_write_time(full, ec);
    auto it = cache.find(full);
    if (it != cache.end() && it->second.time == time) {
        if (errors) *errors = it->second.errors;
        return it->second.program;
    }

    Cached entry;
    entry.time = time;
    entry.fromSource = true;
    auto program = std::make_shared<Program>();
    bool ok = false;
    if (fs::path(full).extension() == ".nlsg") {
        // Визуальный скрипт: граф -> код NLS -> байткод
        visual::Graph graph;
        std::string error;
        if (visual::loadGraph(full, graph, &error)) ok = visual::compileGraph(graph, *program, entry.errors);
        else entry.errors.push_back({0, 0, error});
    } else {
        std::ifstream file(full);
        std::stringstream source;
        source << file.rdbuf();
        ok = compile(source.str(), *program, entry.errors);
    }
    if (ok) entry.program = program;
    if (errors) *errors = entry.errors;
    cache[full] = entry;
    return entry.program;
}

bool ScriptSystem::syncVars(SceneNode& node, std::vector<CompileError>* errors) {
    if (node.script.path.empty()) { node.script.vars.clear(); return true; }
    auto program = load(node.script.path, errors);
    if (!program) return false;   // Скрипт не собрался — значения не трогаем, чтобы не потерять их

    // Значения по умолчанию даёт пробный запуск инициализации
    VM vm;
    vm.init(program.get());
    std::vector<ScriptVar> synced;
    for (size_t i = 0; i < program->globals.size(); i++) {
        const GlobalVar& g = program->globals[i];
        ScriptVar v;
        v.name = g.name;
        v.type = toScriptVarType(g.type);
        const Value& value = vm.globals[i];
        switch (g.type) {
            case Type::Float: v.value.x = value.f; break;
            case Type::Int: case Type::Bool: v.value.x = (float)value.i; break;
            case Type::Vec3: v.value = vec3(value.v[0], value.v[1], value.v[2]); break;
            case Type::String: v.text = vm.string(value); break;
            case Type::Node: v.node = value.i; break;
            default: break;
        }
        for (const ScriptVar& old : node.script.vars) if (old.name == v.name && old.type == v.type) { v = old; break; }
        synced.push_back(v);
    }
    node.script.vars = synced;
    return true;
}

void ScriptSystem::bind(Instance& inst, int node, std::shared_ptr<const Program> program, const VM* previous) {
    inst.program = program;
    inst.reported = false;
    inst.vm.init(program.get());
    inst.vm.self = node;
    inst.evUpdate = program->findFunction("update", true);
    inst.evFixed = program->findFunction("fixedUpdate", true);
    inst.evCollide = program->findFunction("collide", true);
    inst.evEnter = program->findFunction("triggerEnter", true);
    inst.evExit = program->findFunction("triggerExit", true);
    inst.evMessage = program->findFunction("message", true);

    auto assign = [&](size_t index, Type type, const Value& value, const std::string& text) {
        if (type == Type::String) inst.vm.globals[index] = inst.vm.newString(text);
        else inst.vm.globals[index] = value;
    };
    for (size_t i = 0; i < program->globals.size(); i++) {
        const GlobalVar& g = program->globals[i];
        if (previous) {
            // Горячая перезагрузка: переменные с тем же именем и типом сохраняют значения
            const Program& old = *previous->program;
            for (size_t k = 0; k < old.globals.size(); k++) {
                if (old.globals[k].name == g.name && old.globals[k].type == g.type) assign(i, g.type, previous->globals[k], previous->string(previous->globals[k]));
            }
            continue;
        }
        if (node < 0 || node >= (int)sceneGraph.size()) continue;
        for (const ScriptVar& v : sceneGraph[node].script.vars) {
            if (v.name != g.name || toScriptType(v.type) != g.type) continue;
            Value value;
            switch (g.type) {
                case Type::Float: value = Value::Float(v.value.x); break;
                case Type::Int: value = Value::Int((int32_t)std::lround(v.value.x)); break;
                case Type::Bool: value = Value::Int(v.value.x != 0.0f ? 1 : 0); break;
                case Type::Vec3: value = Value::Vec(v.value.x, v.value.y, v.value.z); break;
                case Type::Node: value = Value::Int(v.node); break;
                default: break;
            }
            assign(i, g.type, value, v.text);
        }
    }
}

void ScriptSystem::report(Instance& inst, int node) {
    if (!inst.vm.failed || inst.reported) return;
    inst.reported = true;
    std::string who = node < (int)sceneGraph.size() ? sceneGraph[node].name : "?";
    std::string message = inst.path + " (" + who + "): " + inst.vm.error;
    spdlog::error("[script] {}", message);
    console.push_back("ERROR " + message);
    showToast("Script error: " + inst.path);
}

void ScriptSystem::ensureInstances() {
    if (instances.size() < sceneGraph.size()) instances.resize(sceneGraph.size());
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const std::string& path = sceneGraph[i].script.path;
        if (path.empty()) { instances[i].reset(); continue; }
        if (instances[i] && instances[i]->path == path && instances[i]->generation == sceneGraph[i].generation) continue;

        instances[i] = std::make_unique<Instance>();
        Instance& inst = *instances[i];
        inst.path = path;
        inst.generation = sceneGraph[i].generation;
        std::error_code ec;
        inst.seen = fs::last_write_time(resolve(path), ec);
        std::vector<CompileError> errors;
        auto program = load(path, &errors);
        if (!program) {
            for (const auto& e : errors) console.push_back("ERROR " + path + ":" + std::to_string(e.line) + ":" + std::to_string(e.column) + ": " + e.message);
            spdlog::error("[script] {} did not compile ({} error(s))", path, errors.size());
            showToast("Script error: " + path);
            continue;
        }
        bind(inst, i, program, nullptr);
        report(inst, i);
    }
}

void ScriptSystem::start() {
    registerEngineBindings();
    instances.clear();
    running = true;
    reloadTimer = 0.0f;
    ensureInstances();
}

void ScriptSystem::stop() {
    mailbox.clear();
    running = false;
    instances.clear();
}

void ScriptSystem::invoke(int node, int Instance::*event, const Value* args, int argc) {
    if (!running || node < 0 || node >= (int)instances.size() || !instances[node]) return;
    Instance& inst = *instances[node];
    if (!inst.program || inst.vm.failed || inst.*event < 0) return;
    if (node >= (int)sceneGraph.size() || !sceneGraph[node].worldActive) return;
    inst.vm.call(inst.*event, args, argc);
    report(inst, node);
}

void ScriptSystem::update(float dt) {
    if (!running) return;
    reloadTimer += dt;
    if (reloadTimer > 0.5f) { reloadTimer = 0.0f; hotReload(); }
    ensureInstances();   // Узлы могли появиться из scene.spawn()

    for (int i = 0; i < (int)instances.size(); i++) {
        if (!instances[i] || !instances[i]->program) continue;
        Instance& inst = *instances[i];
        if (inst.vm.failed || i >= (int)sceneGraph.size() || !sceneGraph[i].worldActive) continue;
        if (!inst.started) {
            inst.started = true;
            int start = inst.program->findFunction("start", true);
            if (start >= 0) inst.vm.call(start);
        }
        if (!inst.vm.failed && inst.evUpdate >= 0) {
            Value arg = Value::Float(dt);
            inst.vm.call(inst.evUpdate, &arg, 1);
        }
        report(inst, i);
    }
    deliverMessages();
}

void ScriptSystem::fixedUpdate(float dt) {
    if (!running) return;
    Value arg = Value::Float(dt);
    for (int i = 0; i < (int)instances.size(); i++) {
        if (instances[i] && instances[i]->started) invoke(i, &Instance::evFixed, &arg, 1);
    }
}

void ScriptSystem::send(int node, const std::string& name, float value) {
    if (!running || node < 0 || node >= (int)sceneGraph.size() || mailbox.size() >= 4096) return;
    mailbox.push_back({ node, sceneGraph[node].generation, name, value });
}

void ScriptSystem::broadcast(const std::string& name, float value) {
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        if (i < (int)instances.size() && instances[i] && instances[i]->evMessage >= 0) send(i, name, value);
    }
}

void ScriptSystem::deliverMessages() {
    // Обработчик может послать новые сообщения — они уйдут следующим кругом; круги ограничены от зацикливания
    if (!mailbox.empty()) ensureInstances();   // Получатель мог быть создан в этом же кадре
    for (int round = 0; round < 8 && !mailbox.empty(); round++) {
        std::vector<Message> batch;
        batch.swap(mailbox);
        for (const Message& m : batch) {
            if (m.node >= (int)instances.size() || !instances[m.node] || m.node >= (int)sceneGraph.size()) continue;
            // Получатель удалён, а его место занял другой объект — письмо не ему
            if (sceneGraph[m.node].generation != m.generation || instances[m.node]->generation != m.generation) continue;
            Instance& inst = *instances[m.node];
            if (!inst.program || inst.vm.failed || inst.evMessage < 0) continue;
            if (!inst.started && sceneGraph[m.node].worldActive) {
                // Только что созданный объект: start() раньше первого сообщения
                inst.started = true;
                int start = inst.program->findFunction("start", true);
                if (start >= 0) inst.vm.call(start);
                if (inst.vm.failed) { report(inst, m.node); continue; }
            }
            Value args[2] = { inst.vm.newString(m.name), Value::Float(m.value) };
            invoke(m.node, &Instance::evMessage, args, 2);
        }
    }
    mailbox.clear();
}

void ScriptSystem::onCollide(int self, int other) {
    Value arg = Value::Int(other);
    invoke(self, &Instance::evCollide, &arg, 1);
    if (other >= 0) { Value back = Value::Int(self); invoke(other, &Instance::evCollide, &back, 1); }
}

void ScriptSystem::onTrigger(int trigger, int other, bool entered) {
    Value arg = Value::Int(other);
    invoke(trigger, entered ? &Instance::evEnter : &Instance::evExit, &arg, 1);
    if (other >= 0) { Value zone = Value::Int(trigger); invoke(other, entered ? &Instance::evEnter : &Instance::evExit, &zone, 1); }
}

// Файл скрипта изменился во время игры — пересобираем и подменяем на лету
void ScriptSystem::hotReload() {
    std::error_code ec;
    for (int i = 0; i < (int)instances.size(); i++) {
        if (!instances[i] || i >= (int)sceneGraph.size()) continue;
        Instance& inst = *instances[i];
        std::string full = resolve(inst.path);
        auto time = fs::last_write_time(full, ec);
        if (ec || time == inst.seen) continue;
        inst.seen = time;

        std::vector<CompileError> errors;
        auto program = load(inst.path, &errors);
        if (!program) {
            // Старая версия продолжает работать, пока новую не починят
            for (const auto& e : errors) console.push_back("ERROR " + inst.path + ":" + std::to_string(e.line) + ":" + std::to_string(e.column) + ": " + e.message);
            showToast("Script error: " + inst.path);
            continue;
        }
        if (program == inst.program) continue;
        if (inst.program) {
            VM previous = inst.vm;
            std::shared_ptr<const Program> keepAlive = inst.program;   // previous ссылается на старый байткод
            bind(inst, i, program, &previous);
        } else {
            bind(inst, i, program, nullptr);
        }
        print("reloaded " + inst.path);
    }
}

}
