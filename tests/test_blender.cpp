#include "TestUtil.h"
#include "BlenderExchange.h"
#include "Scene.h"
#include "Json.h"
#include "MathUtils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <map>
#include <chrono>

namespace fs = std::filesystem;

static std::string readAll(const std::string& path) { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

// Что пришлёт Blender, если ничего не трогать: exchange.json в формате result.json
static std::string echoResult(const std::string& exchange, const std::function<void(JsonValue&)>& edit = nullptr) {
    JsonValue root;
    parseJson(readAll(exchange), root);
    std::map<int, std::string> names;
    for (const JsonValue& n : root.get("nodes")->items) names[(int)n.num("id")] = n.str("name");
    if (edit) edit(root);
    std::ostringstream out;
    out << "{\"version\":1,\"kind\":\"sdf\",\"nodes\":[";
    bool first = true;
    for (const JsonValue& n : root.get("nodes")->items) {
        int parent = (int)n.num("parent", -1);
        out << (first ? "" : ",") << "{\"key\":" << jsonString(n.str("name")) << ",\"id\":" << (int)n.num("id", -1)
            << ",\"name\":" << jsonString(n.str("name")) << ",\"parent\":" << jsonString(parent >= 0 ? names[parent] : n.str("parentName"))
            << ",\"type\":\"" << n.str("type") << "\",\"size\":" << n.num("size") << ",\"p2\":" << n.num("p2")
            << ",\"op\":\"" << n.str("op") << "\",\"smooth\":" << n.num("smooth") << ",\"matrix\":[";
        const JsonValue* m = n.get("matrix");
        for (size_t i = 0; i < 16; i++) out << (i ? "," : "") << m->items[i].number;
        const JsonValue* c = n.get("color");
        out << "],\"color\":[" << c->items[0].number << "," << c->items[1].number << "," << c->items[2].number << "]"
            << ",\"metallic\":" << n.num("metallic") << ",\"roughness\":" << n.num("roughness") << "}";
        first = false;
    }
    out << "]}";
    return out.str();
}

static void setNumber(JsonValue& node, const char* key, double value) {
    for (auto& f : node.fields) if (f.first == key) { f.second.number = value; return; }
}
static void setText(JsonValue& node, const char* key, const std::string& value) {
    for (auto& f : node.fields) if (f.first == key) { f.second.text = value; return; }
    JsonValue v; v.kind = JsonValue::String; v.text = value;
    node.fields.emplace_back(key, v);
}

void runBlenderTests() {
    spdlog::set_level(spdlog::level::warn);
    std::string dir = (fs::temp_directory_path() / "nlsdfe_blender_test").string();
    fs::remove_all(dir);

    TEST_SECTION("blender: axes");
    {
        mat4 up = glm::translate(mat4(1.0f), vec3(0, 1, 0));
        CHECK_NEAR(engineToBlender(up)[3].z, 1.0, 1e-6);           // Вверх движка — это +Z Blender
        mat4 toward = glm::translate(mat4(1.0f), vec3(0, 0, 1));
        CHECK_NEAR(engineToBlender(toward)[3].y, -1.0, 1e-6);      // +Z движка — это -Y Blender
        mat4 m = composeMatrix({1, 2, 3}, {0.3f, 0.5f, 0.1f}, {1, 2, 0.5f});
        mat4 back = blenderToEngine(engineToBlender(m));
        float err = 0.0f;
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) err = std::max(err, std::abs(back[c][r] - m[c][r]));
        CHECK(err < 1e-5f);
    }

    TEST_SECTION("blender: unchanged round trip keeps the scene");
    {
        resetScene();
        int child = createNode(SDF_TORUS, "Ring");
        reparent(child, 1);
        sceneGraph[child].localPosition = {0.5f, 1.0f, -0.25f};
        sceneGraph[child].localRotation = {0.4f, 0.2f, 0.0f};
        sceneGraph[child].gpuData.rotationEuler.w = 0.25f;
        sceneGraph[1].localRotation = {0.0f, 0.7f, 0.0f};
        updateSceneCaches();
        std::vector<SceneNode> before = sceneGraph;

        BlenderSession s;
        std::string error;
        CHECK(writeSdfExchange({0, 1, child}, dir, s, &error));
        CHECK(s.exported.size() == 3);
        std::ofstream(s.result) << echoResult(s.exchange);
        CHECK(blenderResultChanged(s));
        size_t undoBefore = undoStack.size();
        CHECK(applySdfResult(s, &error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        CHECK(!blenderResultChanged(s));
        CHECK(undoStack.size() == undoBefore + 1);
        CHECK(sceneGraph.size() == before.size());
        float worst = 0.0f;
        for (size_t i = 0; i < before.size(); i++) {
            worst = std::max(worst, glm::length(sceneGraph[i].localPosition - before[i].localPosition));
            worst = std::max(worst, glm::length(sceneGraph[i].localScale - before[i].localScale));
        }
        CHECK(worst < 1e-4f);
        CHECK(sceneGraph[child].parent == 1);
        CHECK_NEAR(sceneGraph[child].gpuData.rotationEuler.w, 0.25, 1e-5);
        updateSceneCaches();
        CHECK(glm::length(vec3(sceneGraph[child].gpuData.position) - vec3(before[child].gpuData.position)) < 1e-4f);
    }

    TEST_SECTION("blender: edits come back into the scene");
    {
        resetScene();
        updateSceneCaches();
        BlenderSession s;
        CHECK(writeSdfExchange({0, 1}, dir, s));
        std::ofstream(s.result) << echoResult(s.exchange, [](JsonValue& root) {
            JsonValue& nodes = *const_cast<JsonValue*>(root.get("nodes"));
            // Сфера: сдвиг на +2 по Y Blender (это -Z движка), вычитание, переименование
            JsonValue& sphere = nodes.items[0];
            JsonValue* m = const_cast<JsonValue*>(sphere.get("matrix"));
            m->items[7].number += 2.0;
            setText(sphere, "op", "subtract");
            setText(sphere, "name", "Hole");
            // Куб удалён в Blender
            nodes.items.erase(nodes.items.begin() + 1);
            // Новый тор — дочерний к сфере
            JsonValue torus = nodes.items[0];
            setText(torus, "name", "New Ring");
            setNumber(torus, "id", -1);
            setText(torus, "type", "torus");
            setText(torus, "op", "union");
            setText(torus, "parentName", "Hole");
            nodes.items.push_back(torus);
        });
        std::string error;
        CHECK(applySdfResult(s, &error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        int hole = -1, ring = -1, cube = -1;
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            if (sceneGraph[i].name == "Hole") hole = i;
            if (sceneGraph[i].name == "New Ring") ring = i;
            if (sceneGraph[i].name == "Cube") cube = i;
        }
        CHECK(hole >= 0 && ring >= 0 && cube == -1);
        if (hole >= 0) {
            CHECK_NEAR(sceneGraph[hole].localPosition.z, -2.0, 1e-4);
            CHECK_NEAR(sceneGraph[hole].localPosition.y, 0.5, 1e-4);
            CHECK((int)std::lround(sceneGraph[hole].gpuData.params.y) == OP_SUBTRACT);
        }
        if (ring >= 0 && hole >= 0) {
            CHECK(sceneGraph[ring].parent == hole);
            CHECK(nodeType(sceneGraph[ring]) == SDF_TORUS);
            CHECK(glm::length(sceneGraph[ring].localPosition) < 1e-4f);   // Та же мировая точка, что у родителя
        }
        // Второй раунд: тот же объект Blender сопоставляется с уже созданным узлом, не плодя копий
        size_t count = sceneGraph.size();
        std::string same = readAll(s.result);
        std::ofstream(s.result) << same;
        fs::last_write_time(s.result, fs::last_write_time(s.result) + std::chrono::seconds(1));
        CHECK(applySdfResult(s, &error));
        CHECK(sceneGraph.size() == count);
        performUndo();
        performUndo();
        CHECK(sceneGraph.size() == 4);   // Два отката — и сцена снова исходная
    }

    fs::remove_all(dir);
    spdlog::set_level(spdlog::level::info);
    resetScene();
}
