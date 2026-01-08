#include "Common.h"

// --- Global Variables Definition ---

float guiAlpha = 1.0f;
bool showSettingsWindow = false;
int selectedSettingsTab = 0;
bool showToolbar = true;
bool showSidebar = true;
bool showToolSettings = true;
int currentShadingMode = 1;
std::string defaultExportPath = "";

std::vector<ObjectData> sceneObjects;
std::vector<int> selectedIndices;
std::vector<ObjectData> clipboard;
Notification toast;
std::string currentLoadedFile = "Untitled";
NosHeader lastLoadedHeader = {};

GameEditorMode currentEditorMode = EDITOR_OBJECT;
EditorMode currentMode = MODE_MOVE;
GizmoAxis currentGizmoAxis = AXIS_NONE;
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
bool showChangelog = true;

// Инициализация переменной логотипа
void* g_AppLogoID = nullptr;

void showToast(std::string text) {
    toast.message = text;
    toast.timer = 3.0f;
    toast.active = true;
}