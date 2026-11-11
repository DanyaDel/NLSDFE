#include "BlenderExchange.h"
#include "Scene.h"
#include "MathUtils.h"
#include "Json.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include "UvTools.h"

namespace fs = std::filesystem;

namespace {
    const char* TYPE_NAMES[] = { "sphere", "box", "cylinder", "torus", "capsule", "cone", "volume" };
    const char* OP_NAMES[] = { "union", "subtract", "intersect" };

    // Столбцы: куда переходят оси движка. X -> X, Y (вверх) -> Z, Z (к зрителю) -> -Y
    mat4 axes() {
        mat4 c(1.0f);
        c[0] = vec4(1, 0, 0, 0);
        c[1] = vec4(0, 0, 1, 0);
        c[2] = vec4(0, -1, 0, 0);
        return c;
    }

    int typeFromName(const std::string& s) {
        for (int i = 0; i < SDF_TYPE_COUNT; i++) if (s == TYPE_NAMES[i]) return i;
        return SDF_SPHERE;
    }
    int opFromName(const std::string& s) {
        for (int i = 0; i < 3; i++) if (s == OP_NAMES[i]) return i;
        return OP_UNION;
    }

    // Матрица построчно, как её читает mathutils.Matrix
    std::string rows(const mat4& m) {
        std::ostringstream ss;
        ss << "[";
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) ss << (r || c ? "," : "") << m[c][r];
        ss << "]";
        return ss.str();
    }
    mat4 fromRows(const JsonValue* v) {
        mat4 m(1.0f);
        if (!v || v->kind != JsonValue::Array || v->items.size() != 16) return m;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) m[c][r] = (float)v->items[r * 4 + c].number;
        return m;
    }

    long long fileTime(const std::string& path) {
        std::error_code ec;
        auto t = fs::last_write_time(path, ec);
        return ec ? 0 : (long long)t.time_since_epoch().count();
    }

    bool writeText(const std::string& path, const std::string& text) {
        std::ofstream f(path);
        f << text;
        return (bool)f;
    }
}

mat4 engineToBlender(const mat4& m) { return axes() * m * glm::transpose(axes()); }
mat4 blenderToEngine(const mat4& m) { return glm::transpose(axes()) * m * axes(); }

bool writeSdfExchange(const std::vector<int>& nodes, const std::string& dir, BlenderSession& s, std::string* error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    s.mesh = false;
    s.dir = dir;
    s.exchange = (fs::path(dir) / "exchange.json").string();
    s.result = (fs::path(dir) / "result.json").string();
    s.resultTime = fileTime(s.result);   // Старый результат прошлой сессии не применяем
    s.exported.clear(); s.keyToNode.clear(); s.nodeNames.clear();

    std::set<int> set(nodes.begin(), nodes.end());
    std::ostringstream out;
    out << "{\"version\":1,\"kind\":\"sdf\",\"result\":\"result.json\",\"blend\":\"edit.blend\",\"nodes\":[";
    bool first = true;
    for (int idx : nodes) {
        if (idx < 0 || idx >= (int)sceneGraph.size() || !isSdfNode(sceneGraph[idx])) continue;
        const SceneNode& n = sceneGraph[idx];
        int type = nodeType(n);
        int parent = set.count(n.parent) ? n.parent : -1;
        out << (first ? "" : ",") << "{"
            << "\"id\":" << idx << ",\"name\":" << jsonString(n.name) << ",\"type\":\"" << TYPE_NAMES[type] << "\""
            << ",\"parent\":" << parent << ",\"matrix\":" << rows(engineToBlender(calculateWorldMatrix(idx)))
            << ",\"size\":" << (type == SDF_VOLUME ? 1.0f : n.gpuData.params.x) << ",\"p2\":" << shapeParam2(type, n.gpuData.rotationEuler.w)
            << ",\"op\":\"" << OP_NAMES[std::clamp((int)std::lround(n.gpuData.params.y), 0, 2)] << "\",\"smooth\":" << n.gpuData.params.z
            << ",\"color\":[" << n.gpuData.color.r << "," << n.gpuData.color.g << "," << n.gpuData.color.b << "]"
            << ",\"metallic\":" << n.gpuData.params.w << ",\"roughness\":" << n.gpuData.color.w << "}";
        first = false;
        s.exported.push_back(idx);
        s.nodeNames[idx] = n.name;
    }
    out << "]}";
    if (s.exported.empty()) { if (error) *error = "nothing to send: select SDF objects"; return false; }
    if (!writeText(s.exchange, out.str())) { if (error) *error = "cannot write " + s.exchange; return false; }
    return true;
}

bool writeMeshExchange(int node, const std::vector<Vertex>& vertices, const std::string& dir, BlenderSession& s, std::string* error) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    s.mesh = true;
    s.dir = dir;
    s.meshNode = node;
    s.exchange = (fs::path(dir) / "exchange.json").string();
    s.result = (fs::path(dir) / "result.obj").string();
    s.resultTime = fileTime(s.result);
    s.nodeNames.clear();
    s.nodeNames[node] = sceneGraph[node].name;
    if (!writeOBJ((fs::path(dir) / "mesh.obj").string(), vertices)) { if (error) *error = "cannot write mesh.obj"; return false; }
    std::string json = "{\"version\":1,\"kind\":\"mesh\",\"name\":" + jsonString(sceneGraph[node].name) +
                       ",\"obj\":\"mesh.obj\",\"resultObj\":\"result.obj\",\"blend\":\"edit.blend\"}";
    if (!writeText(s.exchange, json)) { if (error) *error = "cannot write " + s.exchange; return false; }
    return true;
}

bool blenderResultChanged(const BlenderSession& s, long long* time) {
    long long t = fileTime(s.result);
    if (time) *time = t;
    return t != 0 && t != s.resultTime;
}

bool applySdfResult(BlenderSession& s, std::string* error) {
    auto fail = [&](const std::string& message) { if (error) *error = message; return false; };
    std::ifstream f(s.result);
    std::stringstream text;
    text << f.rdbuf();
    JsonValue root;
    std::string parseError;
    if (!parseJson(text.str(), root, &parseError)) return fail("result.json: " + parseError);
    const JsonValue* list = root.get("nodes");
    if (!list || list->kind != JsonValue::Array) return fail("result.json has no nodes");
    s.resultTime = fileTime(s.result);

    // Узел движка ещё тот, что ушёл в Blender? (могли удалить или переставить)
    auto valid = [&](int idx) {
        auto it = s.nodeNames.find(idx);
        return idx >= 0 && idx < (int)sceneGraph.size() && it != s.nodeNames.end() && sceneGraph[idx].name == it->second;
    };

    struct Entry { const JsonValue* json; std::string key; int node = -1; mat4 world; };
    std::vector<Entry> entries;
    std::set<int> matched;
    for (const JsonValue& item : list->items) {
        Entry e;
        e.json = &item;
        e.key = item.str("key", item.str("name"));
        auto byKey = s.keyToNode.find(e.key);
        int id = (int)item.num("id", -1);
        if (byKey != s.keyToNode.end() && valid(byKey->second) && !matched.count(byKey->second)) e.node = byKey->second;
        else if (valid(id) && std::count(s.exported.begin(), s.exported.end(), id) && !matched.count(id)) e.node = id;
        if (e.node >= 0) matched.insert(e.node);
        e.world = blenderToEngine(fromRows(item.get("matrix")));
        entries.push_back(e);
    }

    // Удалённые в Blender узлы удаляются и в движке. Удаление само кладёт состояние в undo,
    // остальные правки идут следом и откатываются тем же шагом.
    std::vector<int> removed;
    for (const auto& [idx, name] : s.nodeNames) if (valid(idx) && !matched.count(idx)) removed.push_back(idx);
    if (!removed.empty()) {
        selectedIndices = removed;
        deleteSelectedObject();
        auto shift = [&](int idx) { int n = 0; for (int r : removed) if (r < idx) n++; return idx - n; };
        for (Entry& e : entries) if (e.node >= 0) e.node = shift(e.node);
    } else {
        saveUndo();
    }

    // Новые объекты из Blender
    for (Entry& e : entries) {
        if (e.node >= 0) continue;
        if (sceneGraph.size() >= MAX_OBJECTS) return fail("scene is full");
        SceneNode n;
        n.gpuData.params = vec4(1.0f, 0.0f, 0.1f, 0.0f);
        sceneGraph.push_back(n);
        e.node = (int)sceneGraph.size() - 1;
    }

    std::map<std::string, const Entry*> byKey;
    for (const Entry& e : entries) byKey[e.key] = &e;
    std::set<int> sessionNodes;
    for (const Entry& e : entries) sessionNodes.insert(e.node);

    // Параметры и родители
    for (const Entry& e : entries) {
        SceneNode& n = sceneGraph[e.node];
        const JsonValue& j = *e.json;
        n.name = j.str("name", n.name);
        int type = typeFromName(j.str("type"));
        // Воксельная сетка в Blender не редактируется: тип остаётся, если сетка есть
        if (!(nodeType(n) == SDF_VOLUME && n.volume)) n.gpuData.position.w = (float)(type == SDF_VOLUME ? SDF_BOX : type);
        if (nodeType(n) != SDF_VOLUME) n.gpuData.params.x = (float)j.num("size", n.gpuData.params.x);
        n.gpuData.rotationEuler.w = (float)j.num("p2", n.gpuData.rotationEuler.w);
        n.gpuData.params.y = (float)opFromName(j.str("op"));
        n.gpuData.params.z = (float)j.num("smooth", n.gpuData.params.z);
        n.gpuData.params.w = (float)j.num("metallic", n.gpuData.params.w);
        n.gpuData.color.w = (float)j.num("roughness", n.gpuData.color.w);
        if (const JsonValue* c = j.get("color"); c && c->items.size() >= 3)
            n.gpuData.color = vec4((float)c->items[0].number, (float)c->items[1].number, (float)c->items[2].number, n.gpuData.color.w);

        std::string parentKey = j.str("parent");
        int newParent;
        if (!parentKey.empty() && byKey.count(parentKey)) newParent = byKey[parentKey]->node;
        else if (n.parent >= 0 && !sessionNodes.count(n.parent)) newParent = n.parent;   // Родитель вне обмена — остаётся
        else newParent = -1;
        if (newParent != n.parent) {
            if (n.parent >= 0) {
                auto& kids = sceneGraph[n.parent].children;
                kids.erase(std::remove(kids.begin(), kids.end(), e.node), kids.end());
            }
            n.parent = newParent;
            if (newParent >= 0) sceneGraph[newParent].children.push_back(e.node);
        }
    }

    // Мировые матрицы из Blender -> локальные трансформации
    std::map<int, mat4> worlds;
    for (const Entry& e : entries) worlds[e.node] = e.world;
    for (const Entry& e : entries) {
        SceneNode& n = sceneGraph[e.node];
        mat4 parentWorld(1.0f);
        if (n.parent >= 0) parentWorld = worlds.count(n.parent) ? worlds[n.parent] : calculateWorldMatrix(n.parent);
        decomposeMatrix(glm::inverse(parentWorld) * e.world, n.localPosition, n.localRotation, n.localScale);
    }

    // Следующий результат сопоставляется уже с этими узлами
    s.keyToNode.clear(); s.nodeNames.clear(); s.exported.clear();
    selectedIndices.clear();
    for (const Entry& e : entries) {
        s.keyToNode[e.key] = e.node;
        s.nodeNames[e.node] = sceneGraph[e.node].name;
        s.exported.push_back(e.node);
        selectedIndices.push_back(e.node);
    }
    return true;
}
