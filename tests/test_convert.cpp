#include "TestUtil.h"
#include "Scene.h"
#include "MeshConvert.h"
#include "UvTools.h"
#include "Physics.h"
#include <filesystem>
#include <cmath>

static GpuObject makeObject(int type, vec3 pos, vec3 scale, int op = OP_UNION, vec3 color = vec3(1)) {
    GpuObject o{};
    o.rot0 = vec4(1, 0, 0, (float)type);
    o.rot1 = vec4(0, 1, 0, (float)op);
    o.rot2 = vec4(0, 0, 1, 0.001f);
    o.pos = vec4(pos, 1.0f);
    o.scale = vec4(scale, shapeParam2(type, 0.0f));
    o.color = vec4(color, 0.5f);
    o.bound = vec4(shapeHalfExtents(type, 1.0f, shapeParam2(type, 0.0f)) * scale + vec3(0.25f), 0.0f);
    return o;
}

static double meshArea(const OBJModel& m) {
    double area = 0.0;
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 3)
        area += 0.5 * glm::length(glm::cross(m.vertices[i + 1].pos - m.vertices[i].pos, m.vertices[i + 2].pos - m.vertices[i].pos));
    return area;
}

// Объём по теореме о дивергенции: для замкнутой поверхности с внешними нормалями — положительный
static double meshVolume(const OBJModel& m) {
    double volume = 0.0;
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 3)
        volume += glm::dot(m.vertices[i].pos, glm::cross(m.vertices[i + 1].pos, m.vertices[i + 2].pos)) / 6.0;
    return volume;
}

void runConvertTests() {
    spdlog::set_level(spdlog::level::warn);
    const double pi = 3.14159265358979;

    TEST_SECTION("convert: SDF sphere to mesh");
    {
        std::vector<GpuObject> objects = { makeObject(SDF_SPHERE, {1, 2, 3}, vec3(1), OP_UNION, {0.2f, 0.4f, 0.6f}) };
        OBJModel m;
        vec3 center;
        CHECK(sdfToMesh(objects, 48, m, center));
        CHECK_NEAR(center.x, 1.0, 0.05);
        CHECK_NEAR(center.y, 2.0, 0.05);
        float worst = 0.0f;
        for (const Vertex& v : m.vertices) worst = std::max(worst, std::abs(glm::length(v.pos + center - vec3(1, 2, 3)) - 1.0f));
        CHECK(worst < 0.01f);                                  // Вершины лежат на сфере
        CHECK_NEAR(meshArea(m), 4.0 * pi, 4.0 * pi * 0.03);
        CHECK_NEAR(meshVolume(m), 4.0 / 3.0 * pi, 4.0 / 3.0 * pi * 0.03);   // Положительный объём = нормали наружу
        CHECK_NEAR(m.vertices[0].color.b, 0.6, 1e-6);          // Цвет материала перешёл в вершины
        bool normalsOut = true;
        for (const Vertex& v : m.vertices) if (glm::dot(v.normal, glm::normalize(v.pos + center - vec3(1, 2, 3))) < 0.9f) normalsOut = false;
        CHECK(normalsOut);
    }

    TEST_SECTION("convert: CSG survives conversion");
    {
        // Куб с вычтенной сферой: объём меньше куба на часть шара внутри
        std::vector<GpuObject> objects = {
            makeObject(SDF_BOX, {0, 0, 0}, vec3(1)),
            makeObject(SDF_SPHERE, {0, 1, 0}, vec3(0.8f), OP_SUBTRACT),
        };
        OBJModel m;
        vec3 center;
        CHECK(sdfToMesh(objects, 64, m, center));
        double expected = 8.0 - 0.5 * 4.0 / 3.0 * pi * 0.8 * 0.8 * 0.8;   // Половина шара внутри куба
        CHECK_NEAR(meshVolume(m), expected, expected * 0.03);
    }

    TEST_SECTION("convert: mesh to SDF volume");
    {
        // Сфера из SDF -> меш -> воксели: поле должно совпасть с исходной сферой
        std::vector<GpuObject> objects = { makeObject(SDF_SPHERE, {0, 0, 0}, vec3(1)) };
        OBJModel m;
        vec3 meshCenter;
        CHECK(sdfToMesh(objects, 48, m, meshCenter));
        vec3 center;
        float half = 0.0f;
        auto volume = meshToVolume(m.vertices, 48, center, half);
        CHECK(volume != nullptr);
        if (volume) {
            CHECK(volume->resolution == 48);
            CHECK_NEAR(half, 1.15, 0.03);
            volumeData = volume->distances;
            // Мировая точка -> локальный куб: (p - center) / half; расстояние обратно умножается на half
            auto field = [&](vec3 p) { return sampleVolume(0, 48, (p - center) / half) * half; };
            CHECK_NEAR(field(vec3(0, 0, 0)), -1.0, 0.05);
            CHECK_NEAR(field(vec3(0.5f, 0, 0)), -0.5, 0.05);
            CHECK_NEAR(field(vec3(0, 1, 0)), 0.0, 0.04);
            CHECK_NEAR(field(vec3(0.7f, 0.7f, 0)), 0.7 * std::sqrt(2.0) - 1.0, 0.04);
            CHECK_NEAR(field(vec3(3, 0, 0)), 2.0, 0.1);         // Снаружи сетки — до куба плюс значение на границе

            // Как SDF-объект сцены: тот же результат через sdfObject
            GpuObject o = makeObject(SDF_VOLUME, center, vec3(half));
            o.scale.w = 48;
            o.extra.w = 0;
            CHECK_NEAR(sdfObject(o, vec3(0, 0.5f, 0)), -0.5, 0.05);
            volumeData.clear();
        }
    }

    TEST_SECTION("convert: volume node in scene and file");
    {
        resetScene();
        std::vector<GpuObject> objects = { makeObject(SDF_BOX, {0, 0, 0}, vec3(0.5f)) };
        OBJModel m;
        vec3 meshCenter;
        sdfToMesh(objects, 32, m, meshCenter);
        vec3 center;
        float half;
        auto volume = meshToVolume(m.vertices, 32, center, half);
        SceneNode node;
        node.name = "Voxel box";
        node.gpuData.position.w = (float)SDF_VOLUME;
        node.gpuData.params = vec4(1.0f, 0, 0.001f, 0);
        node.localPosition = {0, 3, 0};
        node.localScale = vec3(half);
        node.volume = volume;
        sceneGraph.push_back(node);
        int idx = (int)sceneGraph.size() - 1;
        updateSceneCaches();
        CHECK(volumeData.size() == volume->distances.size());
        CHECK_NEAR(sceneDistance(vec3(0, 3.0f + 0.5f + 0.25f, 0)), 0.25, 0.04);   // Над верхней гранью
        CHECK(pickNode({ vec3(0, 10, 0), vec3(0, -1, 0) }) == idx);

        std::string path = (std::filesystem::temp_directory_path() / "nlsdfe_test_volume.nos").string();
        std::vector<uint8_t> thumb(NOS_THUMB_W * NOS_THUMB_H * 4, 0);
        CHECK(saveScene(path, thumb));
        resetScene();
        CHECK(loadScene(path, false));
        CHECK(idx < (int)sceneGraph.size() && sceneGraph[idx].volume && sceneGraph[idx].volume->distances == volume->distances);
        updateSceneCaches();
        CHECK_NEAR(sceneDistance(vec3(0, 3.75f, 0)), 0.25, 0.04);
        std::filesystem::remove(path);
    }

    TEST_SECTION("convert: volume as a rigid body");
    {
        resetScene();
        sceneGraph.erase(sceneGraph.begin(), sceneGraph.begin() + 2);   // Остаются пол и камера
        std::vector<GpuObject> objects = { makeObject(SDF_BOX, {0, 0, 0}, vec3(0.4f)) };
        OBJModel m;
        vec3 meshCenter;
        sdfToMesh(objects, 32, m, meshCenter);
        vec3 center;
        float half;
        SceneNode node;
        node.gpuData.position.w = (float)SDF_VOLUME;
        node.gpuData.params = vec4(1.0f, 0, 0.001f, 0);
        node.volume = meshToVolume(m.vertices, 32, center, half);
        node.localPosition = {0, 2, 0};
        node.localScale = vec3(half);
        node.body.type = BODY_DYNAMIC;
        sceneGraph.push_back(node);
        int idx = (int)sceneGraph.size() - 1;
        physics::reset();
        for (int i = 0; i < 240; i++) { updateSceneCaches(); physics::step(1.0f / 60.0f, {}, {}); }
        CHECK_NEAR(sceneGraph[idx].localPosition.y, -0.9 + 0.4, 0.05);   // Лёг на пол нижней гранью
    }

    TEST_SECTION("convert: obj vertex colors round trip");
    {
        std::vector<GpuObject> objects = { makeObject(SDF_SPHERE, {0, 0, 0}, vec3(1), OP_UNION, {0.9f, 0.1f, 0.2f}) };
        OBJModel m;
        vec3 center;
        sdfToMesh(objects, 16, m, center);
        std::string path = (std::filesystem::temp_directory_path() / "nlsdfe_test_colors.obj").string();
        CHECK(writeOBJ(path, m.vertices));
        OBJModel loaded = parseOBJ(path);
        CHECK(loaded.vertices.size() == m.vertices.size());
        if (!loaded.vertices.empty()) CHECK_NEAR(loaded.vertices[0].color.r, 0.9, 1e-4);
        std::filesystem::remove(path);
    }

    spdlog::set_level(spdlog::level::info);
    resetScene();
}
