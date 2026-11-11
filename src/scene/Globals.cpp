#include "Common.h"
#include "Scene.h"

float guiAlpha = 1.0f;
bool showSettingsWindow = false;
int selectedSettingsTab = 0;
bool showToolbar = true;
bool showSidebar = true;
bool showAssetBrowser = true;
int currentShadingMode = 1;
std::string defaultExportPath = "";

std::vector<SceneNode> sceneGraph;
std::vector<GpuObject> gpuObjectCache;
std::vector<GpuLight> gpuLightCache;
WorldSettings world;
std::vector<int> selectedIndices;
std::vector<SceneNode> clipboardNodes;
std::vector<MeshData> meshCache;
Notification toast;
std::string currentLoadedFile = "Untitled";
NosHeader lastLoadedHeader = {};

EditorMode currentMode = MODE_MOVE;
GizmoAxis currentGizmoAxis = AXIS_NONE;
vec4 viewportRect = {0, 0, (float)INITIAL_WIDTH, (float)INITIAL_HEIGHT};
CameraState cam;

bool triggerSceneContextMenu = false;
bool isMouseDragging = false;
bool isBoxSelecting = false;
double mousePressX = 0.0;
double mousePressY = 0.0;
double currentMouseX = 0.0;
double currentMouseY = 0.0;
bool isDraggingGizmo = false;
bool showGizmos = true;
bool showChangelog = false;

void* g_AppLogoID = nullptr;

void showToast(std::string text) {
    toast.message = text;
    toast.timer = 3.0f;
    toast.active = true;
}