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
