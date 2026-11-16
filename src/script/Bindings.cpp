#include "ScriptSystem.h"
#include "GameApi.h"
#include "Scene.h"
#include "Physics.h"
#include "Audio.h"
#include <cmath>
#include <algorithm>

// Здесь перечислено всё, что скрипт может сделать с движком.
// Добавить возможность = дописать одну строку в registerEngineBindings().

namespace nls {

Host host;

namespace {
    using T = Type;
    uint32_t randomState = 0x12345678u;

    SceneNode* nodeOf(VM& vm, const Value& v) {
        if (v.i < 0) { vm.fail("null node"); return nullptr; }
        if (v.i >= (int)sceneGraph.size()) { vm.fail("node no longer exists"); return nullptr; }
        return &sceneGraph[v.i];
    }
    vec3 vec(const Value& v) { return vec3(v.v[0], v.v[1], v.v[2]); }
    Value val(vec3 v) { return Value::Vec(v.x, v.y, v.z); }

    // Свойство узла: чтение и запись одним макросом
    #define NODE_GET(...) [](VM& vm, Value* a, Value& r) { SceneNode* n = nodeOf(vm, a[0]); if (!n) return; r = __VA_ARGS__; }
    #define NODE_SET(...) [](VM& vm, Value* a, Value&) { SceneNode* n = nodeOf(vm, a[0]); if (!n) return; __VA_ARGS__; }
    #define FN(...) [](VM& vm, Value* a, Value& r) { (void)vm; (void)a; (void)r; __VA_ARGS__; }
}

Type toScriptType(int t) {
    switch (t) {
        case SVAR_INT: return Type::Int;
        case SVAR_BOOL: return Type::Bool;
        case SVAR_VEC3: return Type::Vec3;
        case SVAR_STRING: return Type::String;
        case SVAR_NODE: return Type::Node;
        default: return Type::Float;
    }
}

int toScriptVarType(Type t) {
    switch (t) {
        case Type::Int: return SVAR_INT;
        case Type::Bool: return SVAR_BOOL;
        case Type::Vec3: return SVAR_VEC3;
        case Type::String: return SVAR_STRING;
        case Type::Node: return SVAR_NODE;
        default: return SVAR_FLOAT;
    }
}

void registerEngineBindings() {
    Registry& r = registry();
    nodeNameHook = [](int node) { return node < (int)sceneGraph.size() ? sceneGraph[node].name : std::string("?"); };

    // ---------------- Свойства узла ----------------
    r.property("position", T::Vec3, NODE_GET(val(n->localPosition)), NODE_SET(n->localPosition = vec(a[1]); n->body.sleeping = false));
    r.property("worldPosition", T::Vec3, NODE_GET(val(vec3(n->gpuData.position))));
    r.property("rotation", T::Vec3, NODE_GET(val(glm::degrees(n->localRotation))), NODE_SET(n->localRotation = glm::radians(vec(a[1])); n->body.sleeping = false));
    r.property("scale", T::Vec3, NODE_GET(val(n->localScale)), NODE_SET(n->localScale = glm::max(glm::abs(vec(a[1])), vec3(0.001f)); n->body.sleeping = false));
    r.property("name", T::String, NODE_GET(vm.newString(n->name)));
    r.property("visible", T::Bool, NODE_GET(Value::Int(n->active ? 1 : 0)), NODE_SET(n->active = a[1].i != 0));
    r.property("color", T::Vec3, NODE_GET(val(vec3(n->gpuData.color))), NODE_SET(n->gpuData.color = vec4(vec(a[1]), n->gpuData.color.w)));
    r.property("metallic", T::Float, NODE_GET(Value::Float(n->gpuData.params.w)), NODE_SET(n->gpuData.params.w = std::clamp(a[1].f, 0.0f, 1.0f)));
    r.property("roughness", T::Float, NODE_GET(Value::Float(n->gpuData.color.w)), NODE_SET(n->gpuData.color.w = std::clamp(a[1].f, 0.0f, 1.0f)));
    r.property("size", T::Float, NODE_GET(Value::Float(n->gpuData.params.x)), NODE_SET(n->gpuData.params.x = std::max(a[1].f, 0.001f)));
    r.property("parent", T::Node, NODE_GET(Value::Int(n->parent)));
    r.property("running", T::Bool, NODE_GET(Value::Int(n->behaviorOn ? 1 : 0)), NODE_SET(n->behaviorOn = a[1].i != 0));
    r.property("fov", T::Float, NODE_GET(Value::Float(n->cameraFov)), NODE_SET(n->cameraFov = std::clamp(a[1].f, 10.0f, 150.0f)));
    r.property("lightIntensity", T::Float, NODE_GET(Value::Float(n->lightIntensity)), NODE_SET(n->lightIntensity = std::max(a[1].f, 0.0f)));

    // Твёрдое тело
    r.property("dynamic", T::Bool, NODE_GET(Value::Int(n->body.type == BODY_DYNAMIC ? 1 : 0)), NODE_SET(n->body.type = a[1].i ? BODY_DYNAMIC : BODY_STATIC; n->body.sleeping = false));
    r.property("velocity", T::Vec3, NODE_GET(val(n->body.velocity)), NODE_SET(n->body.velocity = vec(a[1]); n->body.sleeping = false; n->body.sleepTimer = 0.0f));
    r.property("angularVelocity", T::Vec3, NODE_GET(val(n->body.angularVelocity)), NODE_SET(n->body.angularVelocity = vec(a[1]); n->body.sleeping = false; n->body.sleepTimer = 0.0f));
    r.property("mass", T::Float, NODE_GET(Value::Float(n->body.mass)), NODE_SET(n->body.mass = std::max(a[1].f, 0.001f)));
    r.property("bounciness", T::Float, NODE_GET(Value::Float(n->body.restitution)), NODE_SET(n->body.restitution = std::clamp(a[1].f, 0.0f, 1.0f)));
    r.property("friction", T::Float, NODE_GET(Value::Float(n->body.friction)), NODE_SET(n->body.friction = std::max(a[1].f, 0.0f)));
    r.property("gravityScale", T::Float, NODE_GET(Value::Float(n->body.gravityScale)), NODE_SET(n->body.gravityScale = a[1].f; n->body.sleeping = false));

    // ---------------- Методы узла ----------------
    r.function("node.applyImpulse", T::Void, {T::Node, T::Vec3}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0])) physics::applyImpulse(a[0].i, vec(a[1])); });
    r.function("node.wake", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0])) physics::wake(a[0].i); });
    r.function("node.distanceTo", T::Float, {T::Node, T::Node}, [](VM& vm, Value* a, Value& res) {
        SceneNode* n = nodeOf(vm, a[0]); SceneNode* o = nodeOf(vm, a[1]);
        if (n && o) res = Value::Float(glm::distance(vec3(n->gpuData.position), vec3(o->gpuData.position)));
    });

    // ---------------- Сцена ----------------
    r.function("scene.find", T::Node, {T::String}, [](VM& vm, Value* a, Value& res) {
        const std::string& name = vm.string(a[0]);
        res = Value::Int(-1);
        for (int i = 0; i < (int)sceneGraph.size(); i++) if (sceneGraph[i].name == name) { res = Value::Int(i); return; }
    });
    r.function("scene.count", T::Int, {}, FN(r = Value::Int((int)sceneGraph.size())));
    r.function("scene.get", T::Node, {T::Int}, FN(r = Value::Int(a[0].i >= 0 && a[0].i < (int)sceneGraph.size() ? a[0].i : -1)));

    // ---------------- Ввод, время, игрок ----------------
    r.function("input.down", T::Bool, {T::String}, FN(r = Value::Int(host.keyDown && host.keyDown(vm.string(a[0])) ? 1 : 0)));
    r.function("input.pressed", T::Bool, {T::String}, FN(r = Value::Int(host.keyPressed && host.keyPressed(vm.string(a[0])) ? 1 : 0)));
    r.function("time.now", T::Float, {}, FN(r = Value::Float(host.time)));
    r.function("time.delta", T::Float, {}, FN(r = Value::Float(host.delta)));

    // ---------------- Звук ----------------
    r.function("node.playSound", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0]) && host.playSound) host.playSound(a[0].i); });
    r.function("node.stopSound", T::Void, {T::Node}, [](VM& vm, Value* a, Value&) { if (nodeOf(vm, a[0]) && host.stopSound) host.stopSound(a[0].i); });
    r.function("audio.play", T::Void, {T::String, T::Float}, FN(
        std::string file = host.resolvePath ? host.resolvePath(vm.string(a[0])) : vm.string(a[0]);
        audio::play(file, a[1].f, false, false)));

    // ---------------- Физика ----------------
    r.function("physics.raycast", T::Node, {T::Vec3, T::Vec3, T::Float}, FN(
        physics::RayHit hit;
        r = Value::Int(physics::raycast(vec(a[0]), vec(a[1]), a[2].f, hit) ? hit.node : -1)));
    r.function("physics.raycastDistance", T::Float, {T::Vec3, T::Vec3, T::Float}, FN(
        physics::RayHit hit;
        r = Value::Float(physics::raycast(vec(a[0]), vec(a[1]), a[2].f, hit) ? hit.distance : -1.0f)));
    r.function("physics.gravity", T::Vec3, {}, FN(r = val(physics::gravity)));
    r.function("physics.setGravity", T::Void, {T::Vec3}, FN(physics::gravity = vec(a[0])));

    // ---------------- Интерфейс, переходы, сохранения, сцены ----------------
    game::registerBindings();

    // ---------------- Вывод ----------------
    r.function("log", T::Void, {T::String}, FN(scripts.print(vm.string(a[0]))));
    r.function("toast", T::Void, {T::String}, FN(showToast(vm.string(a[0]))));

    // ---------------- Математика ----------------
    r.function("sin", T::Float, {T::Float}, FN(r = Value::Float(std::sin(a[0].f))));
    r.function("cos", T::Float, {T::Float}, FN(r = Value::Float(std::cos(a[0].f))));
    r.function("tan", T::Float, {T::Float}, FN(r = Value::Float(std::tan(a[0].f))));
    r.function("atan2", T::Float, {T::Float, T::Float}, FN(r = Value::Float(std::atan2(a[0].f, a[1].f))));
    r.function("sqrt", T::Float, {T::Float}, FN(r = Value::Float(std::sqrt(std::max(a[0].f, 0.0f)))));
    r.function("pow", T::Float, {T::Float, T::Float}, FN(r = Value::Float(std::pow(a[0].f, a[1].f))));
    r.function("abs", T::Float, {T::Float}, FN(r = Value::Float(std::fabs(a[0].f))));
    r.function("floor", T::Float, {T::Float}, FN(r = Value::Float(std::floor(a[0].f))));
    r.function("ceil", T::Float, {T::Float}, FN(r = Value::Float(std::ceil(a[0].f))));
    r.function("round", T::Float, {T::Float}, FN(r = Value::Float(std::round(a[0].f))));
    r.function("sign", T::Float, {T::Float}, FN(r = Value::Float(a[0].f > 0.0f ? 1.0f : a[0].f < 0.0f ? -1.0f : 0.0f)));
    r.function("min", T::Float, {T::Float, T::Float}, FN(r = Value::Float(std::min(a[0].f, a[1].f))));
    r.function("max", T::Float, {T::Float, T::Float}, FN(r = Value::Float(std::max(a[0].f, a[1].f))));
    r.function("clamp", T::Float, {T::Float, T::Float, T::Float}, FN(r = Value::Float(std::min(std::max(a[0].f, a[1].f), a[2].f))));
    r.function("lerp", T::Float, {T::Float, T::Float, T::Float}, FN(r = Value::Float(a[0].f + (a[1].f - a[0].f) * a[2].f)));
    r.function("radians", T::Float, {T::Float}, FN(r = Value::Float(glm::radians(a[0].f))));
    r.function("degrees", T::Float, {T::Float}, FN(r = Value::Float(glm::degrees(a[0].f))));
    r.function("random", T::Float, {}, FN(
        randomState = randomState * 1664525u + 1013904223u;
        r = Value::Float((float)(randomState >> 8) / 16777216.0f)));
    r.function("randomRange", T::Float, {T::Float, T::Float}, FN(
        randomState = randomState * 1664525u + 1013904223u;
        r = Value::Float(a[0].f + (a[1].f - a[0].f) * ((float)(randomState >> 8) / 16777216.0f))));

    r.function("length", T::Float, {T::Vec3}, FN(r = Value::Float(glm::length(vec(a[0])))));
    r.function("distance", T::Float, {T::Vec3, T::Vec3}, FN(r = Value::Float(glm::distance(vec(a[0]), vec(a[1])))));
    r.function("dot", T::Float, {T::Vec3, T::Vec3}, FN(r = Value::Float(glm::dot(vec(a[0]), vec(a[1])))));
    r.function("cross", T::Vec3, {T::Vec3, T::Vec3}, FN(r = val(glm::cross(vec(a[0]), vec(a[1])))));
    r.function("normalize", T::Vec3, {T::Vec3}, FN(
        vec3 v = vec(a[0]); float len = glm::length(v);
        r = val(len > 1e-8f ? v / len : vec3(0.0f))));
    r.function("lerpv", T::Vec3, {T::Vec3, T::Vec3, T::Float}, FN(r = val(glm::mix(vec(a[0]), vec(a[1]), a[2].f))));
}

}
