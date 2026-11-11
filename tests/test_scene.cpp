#include "TestUtil.h"
#include "Scene.h"
#include <filesystem>
#include <fstream>
#include <cstring>

static std::string tempFile(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void runSceneTests() {
    TEST_SECTION("scene: hierarchy and SDF");
    resetScene();
    updateSceneCaches();
    CHECK(sceneGraph.size() == 4);   // Сфера, куб, пол и камера
    CHECK(gpuObjectCache.size() == 3);
    CHECK(collisionCache.size() == 3);
    CHECK(nodeType(sceneGraph[3]) == NODE_CAMERA);
    // Сфера радиуса 1 в (0, 0.5, 0): в центре -1, на поверхности 0
    CHECK_NEAR(sdfObject(gpuObjectCache[0], vec3(0, 0.5f, 0)), -1.0, 1e-4);
    CHECK_NEAR(sdfObject(gpuObjectCache[0], vec3(0, 2.5f, 0)), 1.0, 1e-4);
    // Пол: бокс с полувысотой 0.1 в y = -1, верхняя грань на -0.9
    CHECK_NEAR(sdfObject(gpuObjectCache[2], vec3(5, 0.1f, 5)), 1.0, 1e-4);

    // Дочерний узел наследует трансформацию родителя
    int child = createNode(SDF_SPHERE, "Child");
    sceneGraph[child].localPosition = vec3(0, 0, 0);
    reparent(child, 1);
    sceneGraph[child].localPosition = vec3(1, 0, 0);
    sceneGraph[1].localRotation = vec3(0, 1.5707963f, 0);
    updateSceneCaches();
    vec3 childWorld = vec3(sceneGraph[child].gpuData.position);
    CHECK_NEAR(childWorld.x, 2.0, 1e-3);
    CHECK_NEAR(childWorld.z, -1.0, 1e-3);

    // Выбор лучом: сверху вниз в сферу
    CHECK(pickNode({ vec3(0, 10, 0), vec3(0, -1, 0) }) == 0);
    CHECK(pickNode({ vec3(50, 10, 50), vec3(0, 1, 0) }) == -1);

    TEST_SECTION("scene: undo / redo");
    size_t before = sceneGraph.size();
    selectedIndices = { child };
    deleteSelectedObject();
    CHECK(sceneGraph.size() == before - 1);
    performUndo();
    CHECK(sceneGraph.size() == before);
    performRedo();
    CHECK(sceneGraph.size() == before - 1);
    performUndo();

    TEST_SECTION("scene: .nos round trip");
    sceneGraph[0].name = "Named sphere";
    sceneGraph[0].active = false;
    sceneGraph[child].behavior = BEHAVIOR_ROTATE;
    sceneGraph[child].behaviorSpeed = 3.5f;
    sceneGraph[child].collider.shape = COLLIDER_CAPSULE;
    sceneGraph[child].audio.enabled = true;
    sceneGraph[child].audio.clip = "sounds/beep.wav";
    sceneGraph[child].controller.enabled = true;
    sceneGraph[child].controller.walkSpeed = 2.5f;
    sceneGraph[3].cameraFov = 75.0f;
    world.sunIntensity = 1.25f;
    updateSceneCaches();
    std::vector<SceneNode> saved = sceneGraph;
    std::vector<uint8_t> thumb(NOS_THUMB_W * NOS_THUMB_H * 4, 77);
    std::string path = tempFile("nlsdfe_test_roundtrip.nos");
    CHECK(saveScene(path, thumb));

    resetScene();
    CHECK(loadScene(path, false));
    CHECK(sceneGraph.size() == saved.size());
    if (sceneGraph.size() == saved.size()) {
        CHECK(sceneGraph[0].name == "Named sphere");
        CHECK(sceneGraph[0].active == false);
        CHECK(sceneGraph[child].parent == 1);
        CHECK(sceneGraph[child].behavior == BEHAVIOR_ROTATE);
        CHECK_NEAR(sceneGraph[child].behaviorSpeed, 3.5, 1e-6);
        CHECK(sceneGraph[child].collider.shape == COLLIDER_CAPSULE);
        CHECK(sceneGraph[child].audio.enabled && sceneGraph[child].audio.clip == "sounds/beep.wav");
        CHECK(sceneGraph[child].controller.enabled);
        CHECK_NEAR(sceneGraph[child].controller.walkSpeed, 2.5, 1e-6);
        CHECK_NEAR(sceneGraph[3].cameraFov, 75.0, 1e-6);
        CHECK_NEAR(sceneGraph[child].localPosition.x, 1.0, 1e-6);
        CHECK_NEAR(sceneGraph[1].localRotation.y, 1.5707963, 1e-5);
    }
    CHECK_NEAR(world.sunIntensity, 1.25, 1e-6);
    std::vector<uint8_t> readBack;
    CHECK(readNosThumbnail(path, readBack));
    CHECK(readBack.size() == thumb.size() && readBack[100] == 77);

    // Файл старого формата = тот же файл без блока расширения
    {
        std::ifstream in(path, std::ios::binary);
        NosHeader header; in.read(reinterpret_cast<char*>(&header), sizeof(header));
        size_t legacySize = header.thumbnailOffset + header.thumbnailSize;
        std::vector<char> bytes(legacySize);
        in.seekg(0); in.read(bytes.data(), legacySize);
        std::string legacy = tempFile("nlsdfe_test_legacy.nos");
        std::ofstream(legacy, std::ios::binary).write(bytes.data(), legacySize);
        resetScene();
        CHECK(loadScene(legacy, false));
        CHECK(sceneGraph.size() == saved.size());
        updateSceneCaches();
        // Мировое положение ребёнка восстановлено из мировых трансформаций
        if (sceneGraph.size() == saved.size()) {
            CHECK_NEAR(sceneGraph[child].gpuData.position.x, 2.0, 1e-3);
            CHECK_NEAR(sceneGraph[child].gpuData.position.z, -1.0, 1e-3);
        }
        std::filesystem::remove(legacy);
    }
    CHECK(!loadScene(tempFile("nlsdfe_no_such_file.nos"), false));
    std::filesystem::remove(path);
    resetScene();
}

#include "UvTools.h"

void runUvTests() {
    TEST_SECTION("uv: projections and obj round trip");
    // Квадрат из двух треугольников в плоскости XY
    std::vector<Vertex> quad(6);
    vec3 points[6] = { {0,0,0}, {2,0,0}, {2,1,0}, {0,0,0}, {2,1,0}, {0,1,0} };
    for (int i = 0; i < 6; i++) { quad[i].pos = points[i]; quad[i].normal = {0, 0, 1}; quad[i].color = vec4(1); }

    uvProject(quad, UV_PLANAR_Z);
    CHECK_NEAR(quad[2].uv.x, 1.0, 1e-5);
    CHECK_NEAR(quad[2].uv.y, 1.0, 1e-5);
    CHECK_NEAR(quad[0].uv.x, 0.0, 1e-5);

    uvTransform(quad, vec2(0.0f), vec2(0.5f), 0.0f);          // Сжатие вокруг центра
    CHECK_NEAR(quad[2].uv.x, 0.75, 1e-5);
    CHECK_NEAR(quad[0].uv.y, 0.25, 1e-5);
    uvTransform(quad, vec2(0.0f), vec2(1.0f), 90.0f);
    CHECK_NEAR(quad[2].uv.x, 0.25, 1e-5);                     // (0.75, 0.75) повернулась в (0.25, 0.75)
    CHECK_NEAR(quad[2].uv.y, 0.75, 1e-5);
    uvFit(quad);
    float lo = 1.0f, hi = 0.0f;
    for (const auto& v : quad) { lo = std::min(lo, std::min(v.uv.x, v.uv.y)); hi = std::max(hi, std::max(v.uv.x, v.uv.y)); }
    CHECK_NEAR(lo, 0.0, 1e-5);
    CHECK_NEAR(hi, 1.0, 1e-5);

    uvProject(quad, UV_BOX);
    for (const auto& v : quad) CHECK(v.uv.x >= 2.0f / 3.0f - 1e-5f && v.uv.x <= 1.0f + 1e-5f);   // Грань смотрит вдоль Z — третья полоса

    std::string path = (std::filesystem::temp_directory_path() / "nlsdfe_test_uv.obj").string();
    CHECK(writeOBJ(path, quad));
    OBJModel loaded = parseOBJ(path);
    CHECK(loaded.vertices.size() == quad.size());
    if (loaded.vertices.size() == quad.size()) {
        CHECK_NEAR(loaded.vertices[2].pos.x, 2.0, 1e-5);
        CHECK_NEAR(loaded.vertices[2].uv.x, quad[2].uv.x, 1e-5);
        CHECK_NEAR(loaded.vertices[4].normal.z, 1.0, 1e-5);
    }
    std::filesystem::remove(path);
}
