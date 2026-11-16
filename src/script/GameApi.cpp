#include "GameApi.h"
#include "ScriptSystem.h"
#include "UiLayout.h"
#include "Particles.h"
#include "Animation.h"
#include "Physics.h"
#include "Navigation.h"
#include "Audio.h"
#include "MathUtils.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <map>

namespace nls::game {

State state;

namespace {
    using T = Type;

    enum TweenKind { TWEEN_POSITION, TWEEN_ROTATION, TWEEN_SCALE, TWEEN_COLOR };
    struct Tween { int node; int kind; vec3 from, to; float time, duration; };
    std::vector<Tween> tweens;

    std::map<std::string, std::string> saveData;
    std::string saveFile;
    bool saveDirty = false;

    vec3 vec(const Value& v) { return vec3(v.v[0], v.v[1], v.v[2]); }
    Value val(vec3 v) { return Value::Vec(v.x, v.y, v.z); }

    SceneNode* nodeOf(VM& vm, const Value& v) {
        if (v.i < 0) { vm.fail("null node"); return nullptr; }
        if (v.i >= (int)sceneGraph.size()) { vm.fail("node no longer exists"); return nullptr; }
        return &sceneGraph[v.i];
    }

    vec3 tweenValue(const SceneNode& n, int kind) {
        switch (kind) {
            case TWEEN_POSITION: return n.localPosition;
            case TWEEN_ROTATION: return glm::degrees(n.localRotation);
            case TWEEN_SCALE: return n.localScale;
            default: return vec3(n.gpuData.color);
        }
    }

    void applyTween(SceneNode& n, int kind, vec3 v) {
        switch (kind) {
            case TWEEN_POSITION: n.localPosition = v; break;
            case TWEEN_ROTATION: n.localRotation = glm::radians(v); break;
            case TWEEN_SCALE: n.localScale = glm::max(glm::abs(v), vec3(0.001f)); break;
            default: n.gpuData.color = vec4(v, n.gpuData.color.w); break;
        }
        n.body.sleeping = false;
    }

    // Новый переход того же свойства заменяет прежний
    void startTween(VM& vm, Value* a, int kind) {
        SceneNode* n = nodeOf(vm, a[0]);
        if (!n) return;
        tweens.erase(std::remove_if(tweens.begin(), tweens.end(), [&](const Tween& t) { return t.node == a[0].i && t.kind == kind; }), tweens.end());
        if (a[2].f <= 0.0f) { applyTween(*n, kind, vec(a[1])); return; }
        tweens.push_back({ a[0].i, kind, tweenValue(*n, kind), vec(a[1]), 0.0f, a[2].f });
    }

    // Строки файла сохранения: ключ=значение; переводы строк и '\' в значениях экранируются
    std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) { if (c == '\n') out += "\\n"; else if (c == '\\') out += "\\\\"; else out += c; }
        return out;
    }
    std::string unescape(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { out += s[i + 1] == 'n' ? '\n' : s[i + 1]; i++; }
            else out += s[i];
        }
        return out;
    }

    void put(const std::string& key, const std::string& value) {
        if (key.empty() || key.find('=') != std::string::npos || key.find('\n') != std::string::npos) return;
        auto it = saveData.find(key);
        if (it != saveData.end() && it->second == value) return;
        saveData[key] = value;
        saveDirty = true;
    }

    // Префабы читаются с диска один раз за игру
    std::map<std::string, std::vector<SceneNode>> prefabs;

    const std::vector<SceneNode>* prefab(const std::string& name) {
        auto it = prefabs.find(name);
        if (it != prefabs.end()) return it->second.empty() ? nullptr : &it->second;
        namespace fs = std::filesystem;
        fs::path p(name);
        if (p.extension().empty()) p += ".nos";
        std::vector<SceneNode> nodes;
        std::error_code ec;
        for (const fs::path& candidate : { fs::path("assets") / "prefabs" / p, fs::path("assets") / p, p }) {
            std::string full = host.resolvePath ? host.resolvePath(candidate.generic_string()) : scripts.resolve(candidate.generic_string());
            if (fs::exists(full, ec) && readSceneNodes(full, nodes)) break;
            nodes.clear();
        }
        auto& stored = prefabs[name] = std::move(nodes);
        return stored.empty() ? nullptr : &stored;
    }

    // Действия ввода с клавишами по умолчанию
    const std::map<std::string, std::string> defaultActions = {
        {"forward", "w"}, {"back", "s"}, {"left", "a"}, {"right", "d"}, {"jump", "space"}, {"run", "shift"}, {"fire", "mouse1"}, {"use", "e"},
    };
    std::map<std::string, std::string> actions = defaultActions;
    const std::string noKey;

    // Агент навигации: узел идёт по точкам пути
    struct Agent { int node; uint32_t generation; vec3 target; float speed; std::vector<vec3> path; size_t next = 0; float repath = 0.0f; bool reached = false; };
    std::vector<Agent> agents;

    vec3 worldPosition(int node) { return vec3(calculateWorldMatrix(node)[3]); }

    nav::Query agentQuery(int node, vec3 target) {
        nav::Query q;
        q.from = worldPosition(node);
        q.to = target;
        q.ignoreNode = node;
        const SceneNode& n = sceneGraph[node];
        if (n.controller.enabled) { q.radius = n.controller.radius; q.height = n.controller.height; }
        else {
            // Габариты узла: радиус — по горизонтали, рост — по вертикали
            vec3 half = nodeHalfExtents(node);
            q.radius = std::clamp(std::max(half.x, half.z), 0.15f, 2.0f);
            q.height = std::max(half.y * 2.0f, 0.2f);
        }
        return q;
    }

    bool plan(Agent& agent) {
        agent.next = 0;
        agent.repath = 0.0f;
        return nav::findPath(agentQuery(agent.node, agent.target), agent.path, &agent.reached);
    }

    float axisOf(bool negative, bool positive) { return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f); }
    bool key(const char* name) { return host.keyDown && host.keyDown(name); }

    void hudItem(int kind, Value* a, const std::string& text) {
        if (state.hud.size() >= 512) return;
        HudItem item;
        item.kind = kind;
        item.text = text;
        state.hud.push_back(item);
        (void)a;
    }
}

void reset() {
    tweens.clear();
    state.hud.clear();
    state.pendingScene.clear();
    state.restartRequested = false;
    state.quitRequested = false;
    state.timeScale = 1.0f;
    state.cursorVisible = -1;
    ui::reset();
    particles::clear();
    prefabs.clear();
    agents.clear();
}

bool actionDown(const std::string& action) {
    auto it = actions.find(action);
    return it != actions.end() && host.keyDown && host.keyDown(it->second);
}

bool actionPressed(const std::string& action) {
    auto it = actions.find(action);
    return it != actions.end() && host.keyPressed && host.keyPressed(it->second);
}

const std::string& actionKey(const std::string& action) {
    auto it = actions.find(action);
    return it == actions.end() ? noKey : it->second;
}

void bindAction(const std::string& action, const std::string& key) {
    if (action.empty() || key.empty()) return;
    actions[action] = key;
    put("input." + action, key);   // Переназначение переживает перезапуск игры
}

int activeAgents() { return (int)agents.size(); }

void updateNavigation(float dt) {
    for (size_t i = 0; i < agents.size();) {
        Agent& agent = agents[i];
        bool gone = agent.node >= (int)sceneGraph.size() || sceneGraph[agent.node].generation != agent.generation || sceneGraph[agent.node].destroyed;
        if (gone) { agents.erase(agents.begin() + i); continue; }
        SceneNode& node = sceneGraph[agent.node];
        if (!node.worldActive) { i++; continue; }

        // Мир меняется (двери, платформы) — путь время от времени строится заново
        agent.repath += dt;
        if (agent.repath > 1.0f && !plan(agent)) { agents.erase(agents.begin() + i); continue; }

        vec3 position = worldPosition(agent.node);
        float budget = agent.speed * dt;
        while (budget > 0.0f && agent.next < agent.path.size()) {
            vec3 to = agent.path[agent.next] - position;
            to.y = 0.0f;
            float distance = glm::length(to);
            if (distance <= budget) { position += to; budget -= distance; agent.next++; continue; }
            vec3 dir = to / distance;
            position += dir * budget;
            budget = 0.0f;
            // Лицом по ходу: локальная ось -Z, как у камеры и lookAt
            node.localRotation.y = std::atan2(-dir.x, -dir.z);
        }
        if (node.parent < 0) node.localPosition = position;
        else node.localPosition = vec3(glm::inverse(calculateWorldMatrix(node.parent)) * vec4(position, 1.0f));
        node.body.sleeping = false;

        if (agent.next >= agent.path.size()) agents.erase(agents.begin() + i); else i++;
    }
}

void beginFrame() { state.hud.clear(); }

int activeTweens() { return (int)tweens.size(); }

void updateTweens(float dt) {
    for (size_t i = 0; i < tweens.size();) {
        Tween& t = tweens[i];
        if (t.node >= (int)sceneGraph.size()) { tweens.erase(tweens.begin() + i); continue; }
        t.time += dt;
        float k = std::min(t.time / t.duration, 1.0f);
        float eased = k * k * (3.0f - 2.0f * k);   // Плавный разгон и остановка
        applyTween(sceneGraph[t.node], t.kind, glm::mix(t.from, t.to, eased));
        if (k >= 1.0f) tweens.erase(tweens.begin() + i); else i++;
    }
}

void setSaveFile(const std::string& path) {
    saveFile = path;
    saveData.clear();
    saveDirty = false;
    actions = defaultActions;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        saveData[line.substr(0, eq)] = unescape(line.substr(eq + 1));
    }
    for (const auto& [k, v] : saveData) if (k.compare(0, 6, "input.") == 0 && !v.empty()) actions[k.substr(6)] = v;
}

bool flushSave() {
    if (!saveDirty || saveFile.empty()) return true;
    std::ofstream out(saveFile, std::ios::trunc);
    if (!out.is_open()) return false;
    for (const auto& [k, v] : saveData) out << k << '=' << escape(v) << '\n';
    saveDirty = false;
    return out.good();
}

void registerBindings() {
    Registry& r = registry();

    // ---------------- Интерфейс поверх игры (координаты в долях экрана) ----------------
    r.function("ui.text", T::Void, {T::String, T::Float, T::Float, T::Float, T::Vec3}, [](VM& vm, Value* a, Value&) {
        hudItem(HUD_TEXT, a, vm.string(a[0]));
        if (state.hud.empty()) return;
        HudItem& h = state.hud.back();
        h.x = a[1].f; h.y = a[2].f; h.value = std::clamp(a[3].f, 6.0f, 200.0f); h.color = vec(a[4]);
    });
    r.function("ui.rect", T::Void, {T::Float, T::Float, T::Float, T::Float, T::Vec3}, [](VM&, Value* a, Value&) {
        hudItem(HUD_RECT, a, "");
        if (state.hud.empty()) return;
        HudItem& h = state.hud.back();
        h.x = a[0].f; h.y = a[1].f; h.w = a[2].f; h.h = a[3].f; h.color = vec(a[4]);
    });
    r.function("ui.bar", T::Void, {T::Float, T::Float, T::Float, T::Float, T::Float, T::Vec3}, [](VM&, Value* a, Value&) {
        hudItem(HUD_BAR, a, "");
        if (state.hud.empty()) return;
        HudItem& h = state.hud.back();
        h.x = a[0].f; h.y = a[1].f; h.w = a[2].f; h.h = a[3].f; h.value = std::clamp(a[4].f, 0.0f, 1.0f); h.color = vec(a[5]);
    });
    // Кнопка: рисует себя и возвращает true в кадр щелчка
    r.function("ui.button", T::Bool, {T::String, T::Float, T::Float, T::Float, T::Float}, [](VM& vm, Value* a, Value& res) {
        bool hover = state.mouseX >= a[1].f && state.mouseX <= a[1].f + a[3].f && state.mouseY >= a[2].f && state.mouseY <= a[2].f + a[4].f;
        hudItem(HUD_RECT, a, vm.string(a[0]));
        if (!state.hud.empty()) {
            HudItem& h = state.hud.back();
            h.x = a[1].f; h.y = a[2].f; h.w = a[3].f; h.h = a[4].f;
            h.color = hover ? (state.mouseDown ? vec3(0.16f, 0.32f, 0.6f) : vec3(0.26f, 0.45f, 0.8f)) : vec3(0.16f, 0.18f, 0.24f);
        }
        res = Value::Int(hover && state.mousePressed ? 1 : 0);
    });

    // ---------------- Плавные переходы ----------------
    r.function("node.moveTo", T::Void, {T::Node, T::Vec3, T::Float}, [](VM& vm, Value* a, Value&) { startTween(vm, a, TWEEN_POSITION); });
    r.function("node.rotateTo", T::Void, {T::Node, T::Vec3, T::Float}, [](VM& vm, Value* a, Value&) { startTween(vm, a, TWEEN_ROTATION); });
    r.function("node.scaleTo", T::Void, {T::Node, T::Vec3, T::Float}, [](VM& vm, Value* a, Value&) { startTween(vm, a, TWEEN_SCALE); });
    r.function("node.colorTo", T::Void, {T::Node, T::Vec3, T::Float}, [](VM& vm, Value* a, Value&) { startTween(vm, a, TWEEN_COLOR); });
    r.function("node.isMoving", T::Bool, {T::Node}, [](VM& vm, Value* a, Value& res) {
        if (!nodeOf(vm, a[0])) return;
        bool any = false;
        for (const Tween& t : tweens) if (t.node == a[0].i) any = true;
        res = Value::Int(any ? 1 : 0);
    });

    // ---------------- Ориентация ----------------
    // Узел поворачивается локальной осью -Z к точке (так же смотрит камера)
    r.function("node.lookAt", T::Void, {T::Node, T::Vec3}, [](VM& vm, Value* a, Value&) {
        SceneNode* n = nodeOf(vm, a[0]);
        if (!n) return;
        vec3 d = vec(a[1]) - vec3(n->gpuData.position);
        float len = glm::length(d);
        if (len < 1e-5f) return;
        d /= len;
        n->localRotation = vec3(std::asin(std::clamp(d.y, -1.0f, 1.0f)), std::atan2(-d.x, -d.z), 0.0f);
    });
    r.function("node.forward", T::Vec3, {T::Node}, [](VM& vm, Value* a, Value& res) {
        SceneNode* n = nodeOf(vm, a[0]);
        if (!n) return;
        float pitch = n->localRotation.x, yaw = n->localRotation.y;
        res = val(vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)));
    });
    r.function("node.worldPosition", T::Vec3, {T::Node}, [](VM& vm, Value* a, Value& res) {
        SceneNode* n = nodeOf(vm, a[0]);
        if (n) res = val(vec3(n->gpuData.position));
    });

    // ---------------- Ввод ----------------
    // "horizontal": действия left/right и стрелки, "vertical": back/forward и стрелки; геймпад складывается с клавишами
    r.function("input.axis", T::Float, {T::String}, [](VM& vm, Value* a, Value& res) {
        const std::string& name = vm.string(a[0]);
        float v = 0.0f;
        if (name == "horizontal") v = axisOf(actionDown("left") || key("left"), actionDown("right") || key("right")) + state.padAxes[0];
        else if (name == "vertical") v = axisOf(actionDown("back") || key("down"), actionDown("forward") || key("up")) - state.padAxes[1];
        else { vm.fail("unknown axis '" + name + "' (horizontal, vertical)"); return; }
        res = Value::Float(std::clamp(v, -1.0f, 1.0f));
    });
    r.function("input.mouseX", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(state.mouseX); });
    r.function("input.mouseY", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(state.mouseY); });
    r.function("input.mouseDX", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(state.mouseDX); });
    r.function("input.mouseDY", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(state.mouseDY); });
    // Геймпад: оси 0..5 (левый стик, правый стик, триггеры), кнопки 0..14 в раскладке Xbox
    r.function("input.padAxis", T::Float, {T::Int}, [](VM&, Value* a, Value& res) {
        res = Value::Float(a[0].i >= 0 && a[0].i < 6 ? state.padAxes[a[0].i] : 0.0f);
    });
    r.function("input.padDown", T::Bool, {T::Int}, [](VM&, Value* a, Value& res) {
        res = Value::Int(a[0].i >= 0 && a[0].i < 15 && state.padButtons[a[0].i] ? 1 : 0);
    });
    r.function("input.padPressed", T::Bool, {T::Int}, [](VM&, Value* a, Value& res) {
        res = Value::Int(a[0].i >= 0 && a[0].i < 15 && state.padButtons[a[0].i] && !state.padButtonsBefore[a[0].i] ? 1 : 0);
    });

    // ---------------- Время ----------------
    r.function("time.scale", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(state.timeScale); });
    r.function("time.setScale", T::Void, {T::Float}, [](VM&, Value* a, Value&) { state.timeScale = std::clamp(a[0].f, 0.0f, 10.0f); });
    // true один раз за каждый интервал: if time.every(2.0) { ... }
    r.function("time.every", T::Bool, {T::Float}, [](VM&, Value* a, Value& res) {
        if (a[0].f <= 0.0f) { res = Value::Int(1); return; }
        res = Value::Int(std::floor(host.time / a[0].f) != std::floor((host.time - host.delta) / a[0].f) ? 1 : 0);
    });

    // ---------------- Сохранения ----------------
    r.function("save.setFloat", T::Void, {T::String, T::Float}, [](VM& vm, Value* a, Value&) { put(vm.string(a[0]), std::to_string(a[1].f)); });
    r.function("save.setInt", T::Void, {T::String, T::Int}, [](VM& vm, Value* a, Value&) { put(vm.string(a[0]), std::to_string(a[1].i)); });
    r.function("save.setString", T::Void, {T::String, T::String}, [](VM& vm, Value* a, Value&) { put(vm.string(a[0]), vm.string(a[1])); });
    r.function("save.getFloat", T::Float, {T::String, T::Float}, [](VM& vm, Value* a, Value& res) {
        auto it = saveData.find(vm.string(a[0]));
        res = Value::Float(it == saveData.end() ? a[1].f : (float)std::atof(it->second.c_str()));
    });
    r.function("save.getInt", T::Int, {T::String, T::Int}, [](VM& vm, Value* a, Value& res) {
        auto it = saveData.find(vm.string(a[0]));
        res = Value::Int(it == saveData.end() ? a[1].i : std::atoi(it->second.c_str()));
    });
    r.function("save.getString", T::String, {T::String, T::String}, [](VM& vm, Value* a, Value& res) {
        auto it = saveData.find(vm.string(a[0]));
        res = it == saveData.end() ? a[1] : vm.newString(it->second);
    });
    r.function("save.has", T::Bool, {T::String}, [](VM& vm, Value* a, Value& res) { res = Value::Int(saveData.count(vm.string(a[0])) ? 1 : 0); });
    r.function("save.remove", T::Void, {T::String}, [](VM& vm, Value* a, Value&) { if (saveData.erase(vm.string(a[0]))) saveDirty = true; });
    r.function("save.clear", T::Void, {}, [](VM&, Value*, Value&) { if (!saveData.empty()) saveDirty = true; saveData.clear(); });
    r.function("save.flush", T::Void, {}, [](VM&, Value*, Value&) { flushSave(); });

    // ---------------- Создание и удаление узлов ----------------
    // Копия узла вместе с потомками; место удалённых узлов используется повторно
    r.function("scene.spawn", T::Node, {T::Node}, [](VM& vm, Value* a, Value& res) {
        if (!nodeOf(vm, a[0])) return;
        res = Value::Int(spawnNodes(copySubtree(a[0].i), sceneGraph[a[0].i].parent));
    });
    // Префаб из assets/prefabs/<имя>.nos (или любой .nos проекта) в точке мира
    r.function("scene.instantiate", T::Node, {T::String, T::Vec3}, [](VM& vm, Value* a, Value& res) {
        const std::vector<SceneNode>* nodes = prefab(vm.string(a[0]));
        if (!nodes) { vm.fail("prefab '" + vm.string(a[0]) + "' not found"); return; }
        int root = spawnNodes(*nodes);
        if (root >= 0) sceneGraph[root].localPosition = vec(a[1]);
        res = Value::Int(root);
    });
    // Узел исчезает сразу, место освобождается в конце кадра
    r.function("node.destroy", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0])) destroyNode(a[0].i); });
    r.function("node.isAlive", T::Bool, {T::Node}, [](VM&, Value* a, Value& res) {
        res = Value::Int(a[0].i >= 0 && a[0].i < (int)sceneGraph.size() && !sceneGraph[a[0].i].destroyed ? 1 : 0);
    });
    r.function("node.child", T::Node, {T::Node, T::String}, [](VM& vm, Value* a, Value& res) {
        SceneNode* n = nodeOf(vm, a[0]);
        if (!n) return;
        res = Value::Int(-1);
        // Поиск по имени среди всех потомков
        std::vector<int> queue = n->children;
        for (size_t i = 0; i < queue.size(); i++) {
            const SceneNode& c = sceneGraph[queue[i]];
            if (c.name == vm.string(a[1])) { res = Value::Int(queue[i]); return; }
            queue.insert(queue.end(), c.children.begin(), c.children.end());
        }
    });

    // ---------------- Частицы ----------------
    r.function("node.emit", T::Void, {T::Node, T::Int}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0])) particles::emit(a[0].i, a[1].i); });
    r.property("emitting", T::Bool,
        [](VM& vm, Value* a, Value& res) { if (SceneNode* n = nodeOf(vm, a[0])) res = Value::Int(n->emitter.enabled && n->emitter.emitting ? 1 : 0); },
        [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) { n->emitter.emitting = a[1].i != 0; if (a[1].i) n->emitter.enabled = true; } });

    // ---------------- Анимация по ключам ----------------
    r.function("node.playAnimation", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) anim::play(*n, true); });
    r.function("node.resumeAnimation", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) anim::play(*n, false); });
    r.function("node.stopAnimation", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) anim::stop(*n); });
    r.function("node.isAnimating", T::Bool, {T::Node}, [](VM& vm, Value* a, Value& res) { if (SceneNode* n = nodeOf(vm, a[0])) res = Value::Int(n->animation.playing ? 1 : 0); });
    r.property("animationTime", T::Float,
        [](VM& vm, Value* a, Value& res) { if (SceneNode* n = nodeOf(vm, a[0])) res = Value::Float(n->animation.time); },
        [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) { n->animation.time = a[1].f; anim::apply(*n, a[1].f); } });
    r.property("animationSpeed", T::Float,
        [](VM& vm, Value* a, Value& res) { if (SceneNode* n = nodeOf(vm, a[0])) res = Value::Float(n->animation.speed); },
        [](VM& vm, Value* a, Value&) { if (SceneNode* n = nodeOf(vm, a[0])) n->animation.speed = a[1].f; });

    // Курсор: показать для меню, спрятать для обзора мышью
    r.function("input.showCursor", T::Void, {T::Bool}, [](VM&, Value* a, Value&) { state.cursorVisible = a[0].i ? 1 : 0; });

    ui::registerBindings();

    // ---------------- Действия ввода ----------------
    // Готовые: forward, back, left, right, jump, run, fire, use. Ими же ходит Character Controller.
    r.function("input.action", T::Bool, {T::String}, [](VM& vm, Value* a, Value& res) {
        if (actionKey(vm.string(a[0])).empty()) { vm.fail("unknown input action '" + vm.string(a[0]) + "' (add it with input.bind)"); return; }
        res = Value::Int(actionDown(vm.string(a[0])) ? 1 : 0);
    });
    r.function("input.actionPressed", T::Bool, {T::String}, [](VM& vm, Value* a, Value& res) {
        if (actionKey(vm.string(a[0])).empty()) { vm.fail("unknown input action '" + vm.string(a[0]) + "' (add it with input.bind)"); return; }
        res = Value::Int(actionPressed(vm.string(a[0])) ? 1 : 0);
    });
    r.function("input.bind", T::Void, {T::String, T::String}, [](VM& vm, Value* a, Value&) { bindAction(vm.string(a[0]), vm.string(a[1])); });
    // Действие со своей клавишей по умолчанию: сохранённое игроком переназначение не затирается
    r.function("input.define", T::Void, {T::String, T::String}, [](VM& vm, Value* a, Value&) {
        if (actionKey(vm.string(a[0])).empty() && !vm.string(a[1]).empty()) actions[vm.string(a[0])] = vm.string(a[1]);
    });
    r.function("input.binding", T::String, {T::String}, [](VM& vm, Value* a, Value& res) { res = vm.newString(actionKey(vm.string(a[0]))); });
    // Клавиша, нажатая в этом кадре, или "" — для экрана настроек управления
    r.function("input.lastKey", T::String, {}, [](VM& vm, Value*, Value& res) { res = vm.newString(host.lastKey ? host.lastKey() : std::string()); });

    // ---------------- Сообщения между скриптами ----------------
    // У получателя срабатывает on message(name: string, value: float) — в конце этого же кадра
    r.function("node.send", T::Void, {T::Node, T::String, T::Float}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0])) scripts.send(a[0].i, vm.string(a[1]), a[2].f); });
    r.function("scene.broadcast", T::Void, {T::String, T::Float}, [](VM& vm, Value* a, Value&) { scripts.broadcast(vm.string(a[0]), a[1].f); });

    // ---------------- Музыка и громкость ----------------
    r.function("audio.playMusic", T::Void, {T::String, T::Float}, [](VM& vm, Value* a, Value&) {
        std::string file = host.resolvePath ? host.resolvePath(vm.string(a[0])) : vm.string(a[0]);
        audio::playMusic(file, a[1].f);
    });
    r.function("audio.stopMusic", T::Void, {}, [](VM&, Value*, Value&) { audio::stopMusic(); });
    r.function("audio.setMusicVolume", T::Void, {T::Float}, [](VM&, Value* a, Value&) { audio::setMusicVolume(a[0].f); });
    r.function("audio.setSoundVolume", T::Void, {T::Float}, [](VM&, Value* a, Value&) { audio::setSfxVolume(a[0].f); });
    r.function("audio.musicVolume", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(audio::musicVolume()); });
    r.function("audio.soundVolume", T::Float, {}, [](VM&, Value*, Value& res) { res = Value::Float(audio::sfxVolume()); });

    // ---------------- Поиск пути ----------------
    // Узел сам идёт к точке в обход препятствий; повторный вызов с новой целью меняет маршрут
    r.function("node.navigateTo", T::Void, {T::Node, T::Vec3, T::Float}, [](VM& vm, Value* a, Value&) {
        if (!nodeOf(vm, a[0])) return;
        vec3 target = vec(a[1]);
        for (Agent& agent : agents) {
            if (agent.node != a[0].i) continue;
            agent.speed = a[2].f;
            // Цель почти на месте — маршрут прежний (вызов каждый кадр не должен каждый кадр искать путь)
            if (glm::distance(agent.target, target) < 0.5f) return;
            agent.target = target;
            plan(agent);
            return;
        }
        Agent agent;
        agent.node = a[0].i; agent.generation = sceneGraph[a[0].i].generation; agent.target = target; agent.speed = a[2].f;
        if (plan(agent)) agents.push_back(agent);
    });
    r.function("node.stopNavigation", T::Void, {T::Node}, [](VM&, Value* a, Value&) {
        agents.erase(std::remove_if(agents.begin(), agents.end(), [&](const Agent& agent) { return agent.node == a[0].i; }), agents.end());
    });
    r.function("node.isNavigating", T::Bool, {T::Node}, [](VM&, Value* a, Value& res) {
        bool any = false;
        for (const Agent& agent : agents) if (agent.node == a[0].i) any = true;
        res = Value::Int(any ? 1 : 0);
    });
    // Длина пути в обход препятствий или -1, если дойти нельзя
    r.function("node.pathDistance", T::Float, {T::Node, T::Vec3}, [](VM& vm, Value* a, Value& res) {
        if (!nodeOf(vm, a[0])) return;
        std::vector<vec3> path;
        bool reached = false;
        nav::Query q = agentQuery(a[0].i, vec(a[1]));
        res = Value::Float(nav::findPath(q, path, &reached) && reached ? nav::pathLength(q.from, path) : -1.0f);
    });

    // ---------------- Сцены и игра ----------------
    // Сцена меняется в конце кадра; имя — файл из scenes/ проекта, расширение можно не писать
    r.function("scene.load", T::Void, {T::String}, [](VM& vm, Value* a, Value&) { state.pendingScene = vm.string(a[0]); });
    r.function("scene.restart", T::Void, {}, [](VM&, Value*, Value&) { state.restartRequested = true; });
    r.function("game.quit", T::Void, {}, [](VM&, Value*, Value&) { state.quitRequested = true; });
}

}
