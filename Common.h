#pragma once
#include <vector>
#include <string>
#include <cstdint>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cmath>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

const int INITIAL_WIDTH = 1280;
const int INITIAL_HEIGHT = 720;
const std::string SHADER_SOURCE = "shaders/shader.comp";
const std::string SHADER_BINARY = "shader.spv";
const std::string ENGINE_VERSION = "v1.3.0";
const int MAX_OBJECTS = 128;   // Лимит узлов сцены (и ширина битовой маски отсечения в шейдере)
const int MAX_LIGHTS = 8;      // Точечные источники света

// --- Math Types ---
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;

struct Ray { vec3 origin; vec3 dir; };
struct CameraBasis { vec3 pos, fwd, right, up; };

// VMA должен обходиться функциями Vulkan 1.0 — так движок стартует и на старых драйверах
#define VMA_VULKAN_VERSION 1000000
#include "vk_mem_alloc.h"

// --- Polygon Types ---
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec4 color;
};

struct MeshBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    uint32_t count;
};

struct MeshData {
    MeshBuffer vertexBuffer;
    MeshBuffer indexBuffer;
    int materialId = -1;
    bool isLoaded = false;
    std::string path;
    vec3 boundsMin = {0, 0, 0};
    vec3 boundsMax = {0, 0, 0};
};

// --- Scene Structs ---

// position.w хранит тип узла
enum NodeType {
    NODE_LIGHT = -2,   // Точечный свет
    NODE_EMPTY = -1,   // Пустышка / контейнер / полигональный меш
    SDF_SPHERE = 0,
    SDF_BOX,
    SDF_CYLINDER,
    SDF_TORUS,
    SDF_CAPSULE,
    SDF_CONE,
    SDF_TYPE_COUNT
};

enum CsgOp { OP_UNION = 0, OP_SUBTRACT, OP_INTERSECT };

// Формат узла в .nos (менять раскладку нельзя — сломаются старые файлы)
struct ObjectData {
    mat4 invRotation;
    vec4 position;      // xyz = мировая позиция, w = NodeType
    vec4 scale;         // xyz = мировой масштаб
    vec4 params;        // x = размер, y = CsgOp, z = сглаживание, w = metallic
    vec4 color;         // rgb, w = roughness
    vec4 rotationEuler; // xyz = мировые углы, w = доп. параметр формы (толщина тора и т.п.)
};

// То, что реально уходит в шейдер: только SDF-объекты, плотно упакованные
struct GpuObject {
    vec4 rot0;   // столбцы обратного поворота; w = тип
    vec4 rot1;   // w = CsgOp
    vec4 rot2;   // w = сглаживание
    vec4 pos;    // xyz, w = размер
    vec4 scale;  // xyz, w = доп. параметр формы
    vec4 color;  // rgb, w = roughness
    vec4 bound;  // xyz = полуразмеры ограничивающего бокса (с запасом), w = metallic
    vec4 extra;  // x = выделен, y = индекс узла в sceneGraph
};

struct GpuLight {
    vec4 posRange;   // xyz, w = радиус действия
    vec4 color;      // rgb * intensity
};

enum BehaviorType { BEHAVIOR_NONE = 0, BEHAVIOR_ROTATE, BEHAVIOR_PINGPONG, BEHAVIOR_COUNT };
enum TriggerAction { TRIGGER_TOGGLE_VISIBLE = 0, TRIGGER_START_BEHAVIOR, TRIGGER_STOP_BEHAVIOR, TRIGGER_TOGGLE_BEHAVIOR, TRIGGER_ACTION_COUNT };

struct SceneNode {
    std::string name;
    vec3 localPosition = {0, 0, 0};
    vec3 localRotation = {0, 0, 0};
    vec3 localScale    = {1, 1, 1};
    ObjectData gpuData;
    int parent = -1;
    std::vector<int> children;
    bool isOpen = true;
    int meshIdx = -1; // -1 means SDF-only or container
    std::string meshPath;

    bool active = true;          // Скрытые узлы не рендерятся и не участвуют в коллизиях
    bool worldActive = true;     // active с учётом родителей (пересчитывается каждый кадр)

    // Свет (NODE_LIGHT)
    float lightIntensity = 5.0f;
    float lightRange = 10.0f;

    // Компонент логики (работает в Play Mode)
    int behavior = BEHAVIOR_NONE;
    vec3 behaviorVec = {0, 1, 0};   // Rotate: рад/с по осям; PingPong: смещение
    float behaviorSpeed = 1.0f;
    bool behaviorOn = true;

    // Триггер: невидимая зона в форме бокса узла
    bool isTrigger = false;
    int triggerTarget = -1;
    int triggerAction = TRIGGER_TOGGLE_VISIBLE;

    SceneNode() {
        gpuData.invRotation = mat4(1.0f);
        gpuData.position = vec4(0, 0, 0, (float)NODE_EMPTY);
        gpuData.scale = vec4(1, 1, 1, 0);
        gpuData.params = vec4(1.0f, 0, 0.1f, 0);
        gpuData.color = vec4(1, 1, 1, 0.5f);
        gpuData.rotationEuler = vec4(0);
    }
};

inline int nodeType(const SceneNode& n) { return (int)std::lround(n.gpuData.position.w); }
inline bool isSdfNode(const SceneNode& n) { return nodeType(n) >= 0 && n.meshIdx < 0; }

struct NosHeader {
    char magic[4];
    uint32_t version;
    uint32_t objectCount;
    uint32_t checksum;
    uint32_t thumbnailOffset;
    uint32_t thumbnailSize;
    float reserved[10];
};

struct SceneMetadata {
    vec3 target;
    float theta;
    float phi;
    float radius;
    float padding[4];
};

// Освещение и небо сцены (сохраняется в .nos)
struct WorldSettings {
    float sunAzimuth = 0.785f;
    float sunElevation = 0.847f;
    float sunIntensity = 2.8f;
    vec3 sunColor = {1.0f, 0.96f, 0.9f};
    vec3 skyTop = {0.22f, 0.36f, 0.62f};
    vec3 skyHorizon = {0.62f, 0.70f, 0.80f};
    float ambient = 0.35f;
};

inline vec3 sunDirection(const WorldSettings& w) {
    return { std::cos(w.sunElevation) * std::sin(w.sunAzimuth), std::sin(w.sunElevation), -std::cos(w.sunElevation) * std::cos(w.sunAzimuth) };
}

struct Notification {
    std::string message;
    float timer = 0.0f;
    bool active = false;
};

struct CameraState {
    vec3 target = {0.0f, 0.5f, 0.0f};
    float theta = 0.0f;
    float phi = 1.3f;
    float radius = 7.0f;
    bool leftMouse = false, rightMouse = false, middleMouse = false;
    double lastX = 0.0, lastY = 0.0;
};

// --- Enums ---
enum GameEditorMode { EDITOR_OBJECT, EDITOR_MAP, EDITOR_GAME_BUILD };
enum EditorMode { MODE_MOVE, MODE_ROTATE, MODE_SCALE };
enum GizmoAxis { AXIS_NONE, AXIS_X, AXIS_Y, AXIS_Z, AXIS_CENTER };
enum ShadingMode { SHADING_WIRE = 0, SHADING_SOLID, SHADING_MATERIAL, SHADING_RENDERED };

// --- Globals (Defined in Globals.cpp) ---
extern float guiAlpha;
extern bool showSettingsWindow;
extern int selectedSettingsTab;
extern bool showToolbar;
extern bool showSidebar;
extern bool showAssetBrowser;
extern bool showChangelog;
extern int currentShadingMode;
extern std::string defaultExportPath;
extern bool triggerSceneContextMenu;

extern bool isMouseDragging;
extern bool isBoxSelecting;
extern double mousePressX;
extern double mousePressY;
extern double currentMouseX;
extern double currentMouseY;
extern bool isDraggingGizmo;
extern bool showGizmos;

inline constexpr float DRAG_THRESHOLD = 3.0f;

extern GameEditorMode currentEditorMode;
extern EditorMode currentMode;
extern GizmoAxis currentGizmoAxis;
extern vec4 viewportRect;   // x, y, w, h — область 3D-вьюпорта в пикселях окна

extern CameraState cam;
extern WorldSettings world;
extern std::vector<SceneNode> sceneGraph;
extern std::vector<int> selectedIndices;
extern std::vector<SceneNode> clipboardNodes;
extern std::vector<GpuObject> gpuObjectCache;   // Обновляется updateSceneCaches()
extern std::vector<GpuLight> gpuLightCache;
extern std::vector<MeshData> meshCache;
extern Notification toast;
extern std::string currentLoadedFile;
extern NosHeader lastLoadedHeader;

extern void* g_AppLogoID;

void showToast(std::string text);
CameraBasis getCameraBasis();
float getCameraFocalLength();
bool projectWorldToScreen(vec3 worldPos, vec2& outScreen);
