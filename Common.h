#pragma once
#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <cmath>

const int INITIAL_WIDTH = 1280;
const int INITIAL_HEIGHT = 720;
const std::string SHADER_SOURCE = "shaders/shader.comp";
const std::string SHADER_BINARY = "shader.spv";
const int MAX_OBJECTS = 100;

// --- Math Types ---
struct vec2 { float x, y; };
struct vec3 { float x, y, z; };
struct vec4 { float x, y, z, w; };
struct mat4 { float m[16]; };

struct Ray { vec3 origin; vec3 dir; };
struct CameraBasis { vec3 pos, fwd, right, up; };

// --- Scene Structs ---
struct ObjectData {
    mat4 invRotation;
    vec4 position; // w = Object Type ID
    vec4 scale;    
    vec4 params;   
    vec4 color;    
    vec4 rotationEuler; 
};

struct SceneNode {
    std::string name;
    vec3 localPosition = {0, 0, 0};
    vec3 localRotation = {0, 0, 0};
    vec3 localScale    = {1, 1, 1};
    ObjectData gpuData;
    int parent = -1;
    std::vector<int> children;
    bool isOpen = true;
};

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

struct PushConstants {
    float camToWorld[16];
    vec2 resolution;
    float time;
    int draggingAxis;
    int objectCount;
    int selectedIndex; 
    int mode; 
    float uiMinX, uiMinY, uiMaxX, uiMaxY; 
    int shadingMode; 
    float gizmoPos[3]; 
};

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

// --- Globals (Defined in Globals.cpp) ---
extern float guiAlpha;
extern bool showSettingsWindow;
extern int selectedSettingsTab;
extern bool showToolbar;
extern bool showSidebar;
extern bool showToolSettings;
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

extern CameraState cam;
// sceneObjects УДАЛЕН - используем только sceneGraph
extern std::vector<SceneNode> sceneGraph; 
extern std::vector<int> selectedIndices;
extern std::vector<SceneNode> clipboardNodes; // Изменено с ObjectData на SceneNode
extern std::vector<ObjectData> gpuObjectCache;
extern Notification toast;
extern std::string currentLoadedFile;
extern NosHeader lastLoadedHeader;

extern void* g_AppLogoID; 

void showToast(std::string text);
CameraBasis getCameraBasis();
bool projectWorldToScreen(vec3 worldPos, float width, float height, vec2& outScreen);