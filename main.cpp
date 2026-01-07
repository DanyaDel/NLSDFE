/*
   SDF Engine - Main Source (Optimized)
   Includes: Vulkan Renderer, Viewport Shading, SDF Logic, ImGui UI, NOS File Format Support, Win32 File Dialogs
*/

// --- WINDOWS INCLUDES FOR FILE DIALOGS ---
#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <commdlg.h>
    #include <shlobj.h> // НУЖНО ДЛЯ ВЫБОРА ПАПКИ
    // Для получения нативного дескриптора окна (HWND) из GLFW
    #define GLFW_EXPOSE_NATIVE_WIN32
#endif

// --- STANDARD INCLUDES ---
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#ifdef _WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cstring>
#include <optional>
#include <array>
#include <numeric>

// --- ImGui Includes ---
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace fs = std::filesystem;

// =================================================================
// --- Config & Globals ---
// =================================================================
const int INITIAL_WIDTH = 1280;
const int INITIAL_HEIGHT = 720;
const std::string SHADER_SOURCE = "shaders/shader.comp";
const std::string SHADER_BINARY = "shader.spv";
const int MAX_OBJECTS = 100;

// UI Globals
float guiAlpha = 1.0f;
bool showSettingsWindow = false; 
int selectedSettingsTab = 0;   
bool triggerSceneContextMenu = false;

// Shading Modes
// 0=Wire/Heat, 1=Solid, 2=Material, 3=Rendered
int currentShadingMode = 1; 

// Settings Globals
std::string defaultExportPath = ""; 

// Panels Visibility
bool showToolbar = true;
bool showSidebar = true; 
bool showToolSettings = true;

// Editor Modes
enum GameEditorMode { EDITOR_OBJECT, EDITOR_MAP, EDITOR_GAME_BUILD };
GameEditorMode currentEditorMode = EDITOR_OBJECT;

// Input Logic Globals
bool isMouseDragging = false;
bool isBoxSelecting = false;
double mousePressX = 0.0;
double mousePressY = 0.0;
double currentMouseX = 0.0;
double currentMouseY = 0.0;
const float DRAG_THRESHOLD = 3.0f;

// =================================================================
// --- File Dialog Helper (Win32) ---
// =================================================================
class FileDialogs {
public:
    static std::string OpenFile(GLFWwindow* window, const char* filter) {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        char szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(window);
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
#endif
        return std::string();
    }

    static std::string SaveFile(GLFWwindow* window, const char* filter, const std::string& initialDir = "") {
#ifdef _WIN32
        OPENFILENAMEA ofn;
        char szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = glfwGetWin32Window(window);
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        
        if (!initialDir.empty()) {
            ofn.lpstrInitialDir = initialDir.c_str();
        }

        strncpy(szFile, "scene.nos", sizeof(szFile) - 1);
        if (GetSaveFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
#endif
        return std::string();
    }

    static std::string SelectFolder(GLFWwindow* window) {
#ifdef _WIN32
        char path[MAX_PATH];
        BROWSEINFOA bi = { 0 };
        bi.lpszTitle = "Select Default Export Folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.hwndOwner = glfwGetWin32Window(window);
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != 0) {
            if (SHGetPathFromIDListA(pidl, path)) {
                CoTaskMemFree(pidl); 
                return std::string(path);
            }
            CoTaskMemFree(pidl);
        }
#endif
        return std::string();
    }
};

// =================================================================
// --- Math Helpers ---
// =================================================================
struct vec2 { float x, y; };
struct vec3 { float x, y, z; };
struct vec4 { float x, y, z, w; };
struct mat4 { float m[16]; };

vec3 operator+(vec3 a, vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
vec3 operator-(vec3 a, vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
vec3 operator*(vec3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
vec3 operator*(float b, vec3 a) { return {a.x*b, a.y*b, a.z*b}; }
vec3 operator*(vec3 a, vec3 b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
vec3 operator-(vec3 v) { return {-v.x, -v.y, -v.z}; }
vec3& operator+=(vec3& a, vec3 b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
vec3& operator-=(vec3& a, vec3 b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }

float dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
vec3 cross(vec3 a, vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
float length(vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
vec3 normalize(vec3 v) { float l = length(v); if(l==0) return {0,0,0}; return {v.x/l, v.y/l, v.z/l}; }

mat4 createInverseRotationMatrix4x4(vec3 r) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    float m00 = cy * cz;             float m01 = cz * sx * sy - cx * sz;   float m02 = cx * cz * sy + sx * sz;
    float m10 = cy * sz;             float m11 = cx * cz + sx * sy * sz;   float m12 = -cz * sx + cx * sy * sz;
    float m20 = -sy;                 float m21 = cy * sx;                  float m22 = cx * cy;
    mat4 res;
    res.m[0] = m00; res.m[1] = m01; res.m[2] = m02; res.m[3] = 0.0f;
    res.m[4] = m10; res.m[5] = m11; res.m[6] = m12; res.m[7] = 0.0f;
    res.m[8] = m20; res.m[9] = m21; res.m[10]= m22; res.m[11]= 0.0f;
    res.m[12]= 0.0f; res.m[13]=0.0f; res.m[14]=0.0f; res.m[15]=1.0f;
    return res;
}

void createRotationMatrix3x3(vec3 r, float M[9]) {
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    M[0] = cy * cz;             M[1] = cz * sx * sy - cx * sz;   M[2] = cx * cz * sy + sx * sz;
    M[3] = cy * sz;             M[4] = cx * cz + sx * sy * sz;   M[5] = -cz * sx + cx * sy * sz;
    M[6] = -sy;                 M[7] = cy * sx;                  M[8] = cx * cy;
}

vec3 mul(float M[9], vec3 v) {
    return { M[0]*v.x + M[1]*v.y + M[2]*v.z, M[3]*v.x + M[4]*v.y + M[5]*v.z, M[6]*v.x + M[7]*v.y + M[8]*v.z };
}
void transpose(float M[9], float T[9]) {
    T[0]=M[0]; T[1]=M[3]; T[2]=M[6]; T[3]=M[1]; T[4]=M[4]; T[5]=M[7]; T[6]=M[2]; T[7]=M[5]; T[8]=M[8];
}

vec3 rotateVector(vec3 v, vec3 axis, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0f - c);
}

// =================================================================
// --- Data Structures & NOS Format ---
// =================================================================
struct ObjectData {
    mat4 invRotation;
    vec4 position; // w = Object Type ID
    vec4 scale;    
    vec4 params;   
    vec4 color;    
    vec4 rotationEuler; 
};

// --- NOS HEADER & METADATA ---
struct NosHeader {
    char magic[4];           // "NOS!"
    uint32_t version;        // 100
    uint32_t objectCount;
    uint32_t checksum;
    uint32_t thumbnailOffset;
    uint32_t thumbnailSize;
    float reserved[10];      // Padding to 64 bytes
};

struct SceneMetadata {
    vec3 target;
    float theta;
    float phi;
    float radius;
    float padding[4];        // Align 16 bytes
};

uint32_t calculateChecksum(const std::vector<ObjectData>& objects) {
    if (objects.empty()) return 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(objects.data());
    size_t size = objects.size() * sizeof(ObjectData);
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += data[i];
    return sum;
}

struct PushConstants {
    float camToWorld[16];
    vec2 resolution;
    float time;
    int draggingAxis;
    int objectCount;
    int selectedIndex; 
    int mode; 
    float uiMinX, uiMinY, uiMaxX, uiMaxY; 
    int shadingMode; // NEW: 0=Wire, 1=Solid, 2=Material, 3=Rendered
};

std::vector<ObjectData> sceneObjects;
std::vector<int> selectedIndices; 

// --- Undo System ---
struct UndoState {
    std::vector<ObjectData> objects;
    std::vector<int> selection;
};
std::vector<UndoState> undoStack;

void saveUndo() {
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin()); 
    undoStack.push_back({sceneObjects, selectedIndices});
}

void performUndo() {
    if (undoStack.empty()) return;
    UndoState state = undoStack.back();
    undoStack.pop_back();
    sceneObjects = state.objects;
    selectedIndices = state.selection;
}

enum EditorMode { MODE_MOVE, MODE_ROTATE, MODE_SCALE };
EditorMode currentMode = MODE_MOVE;

enum GizmoAxis { AXIS_NONE, AXIS_X, AXIS_Y, AXIS_Z, AXIS_CENTER };
GizmoAxis currentGizmoAxis = AXIS_NONE;
bool isDraggingGizmo = false;
bool showGizmos = true;

struct CameraState {
    vec3 target = {0.0f, 0.5f, 0.0f};
    float theta = 0.0f; 
    float phi = 1.3f; 
    float radius = 7.0f;
    bool leftMouse = false, rightMouse = false, middleMouse = false;
    double lastX = 0.0, lastY = 0.0;
} cam;

// =================================================================
// --- Camera & Intersection Logic ---
// =================================================================
struct CameraBasis { vec3 pos, fwd, right, up; };

CameraBasis getCameraBasis() {
    cam.phi = std::clamp(cam.phi, 0.01f, 3.14159f - 0.01f);
    vec3 offset = { 
        cam.radius * std::sin(cam.phi) * std::sin(cam.theta), 
        cam.radius * std::cos(cam.phi), 
        cam.radius * std::sin(cam.phi) * std::cos(cam.theta) 
    };
    vec3 pos = cam.target + offset;
    vec3 fwd = normalize(cam.target - pos); 
    vec3 worldUp = {0.0f, 1.0f, 0.0f};
    vec3 right = normalize(cross(fwd, worldUp));
    vec3 up = normalize(cross(right, fwd)); 
    return {pos, fwd, right, up};
}

void updateCameraMatrix(float* m) {
    CameraBasis b = getCameraBasis();
    m[0] = b.right.x; m[1] = b.right.y; m[2] = b.right.z; m[3] = 0.0f;
    m[4] = b.up.x;    m[5] = b.up.y;    m[6] = b.up.z;    m[7] = 0.0f;
    m[8] = -b.fwd.x;  m[9] = -b.fwd.y;  m[10]= -b.fwd.z;  m[11]= 0.0f;
    m[12]= b.pos.x;   m[13]= b.pos.y;   m[14]= b.pos.z;   m[15]= 1.0f;
}

bool projectWorldToScreen(vec3 worldPos, float width, float height, vec2& outScreen) {
    CameraBasis b = getCameraBasis();
    vec3 rel = worldPos - b.pos;
    vec3 viewPos;
    viewPos.x = dot(rel, b.right);
    viewPos.y = dot(rel, b.up);
    viewPos.z = dot(rel, -b.fwd); 
    if (viewPos.z >= 0.0f) return false; 
    float f = 1.8f; 
    float x = viewPos.x * f / -viewPos.z;
    float y = viewPos.y * f / -viewPos.z;
    outScreen.x = (x * height + width) * 0.5f;
    outScreen.y = (height - y * height) * 0.5f;
    return true;
}

vec3 getSelectionCenter() {
    if (selectedIndices.empty()) return {0,0,0};
    vec3 center = {0,0,0};
    for (int idx : selectedIndices) {
        if(idx < sceneObjects.size()) {
            center.x += sceneObjects[idx].position.x;
            center.y += sceneObjects[idx].position.y;
            center.z += sceneObjects[idx].position.z;
        }
    }
    center.x /= (float)selectedIndices.size();
    center.y /= (float)selectedIndices.size();
    center.z /= (float)selectedIndices.size();
    return center;
}

struct Ray { vec3 origin; vec3 dir; };

Ray getRayFromMouse(float mx, float my, float width, float height) {
    CameraBasis basis = getCameraBasis();
    float ndcX = (2.0f * mx) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * my) / height; 
    float aspect = width / height;
    vec3 viewDir = { ndcX * aspect, ndcY, -1.8f }; 
    vec3 worldDir = basis.right * viewDir.x + basis.up * viewDir.y + (-basis.fwd) * viewDir.z;
    return { basis.pos, normalize(worldDir) };
}

float intersectSphere(Ray r, vec3 pos, float rad) {
    vec3 oc = r.origin - pos;
    float b = dot(oc, r.dir);
    float c = dot(oc, oc) - rad*rad;
    float h = b*b - c;
    if(h < 0.0f) return -1.0f;
    return -b - std::sqrt(h);
}

float intersectBox(Ray r, vec3 pos, vec3 size) {
    vec3 ro = r.origin - pos;
    vec3 m = {1.0f/r.dir.x, 1.0f/r.dir.y, 1.0f/r.dir.z};
    vec3 n = m * ro;
    vec3 k = {std::abs(m.x)*size.x, std::abs(m.y)*size.y, std::abs(m.z)*size.z};
    vec3 t1 = -n - k; vec3 t2 = -n + k;
    float tN = std::max(std::max(t1.x, t1.y), t1.z);
    float tF = std::min(std::min(t2.x, t2.y), t2.z);
    if(tN > tF || tF < 0.0f) return -1.0f;
    return tN;
}

float intersectBoxOBB(Ray r, vec3 pos, vec3 rot, vec3 scale, float size) {
    float Mat[9], InvMat[9];
    createRotationMatrix3x3(rot, Mat);
    transpose(Mat, InvMat);
    vec3 rayOrigin = mul(InvMat, r.origin - pos);
    vec3 rayDir = mul(InvMat, r.dir);
    vec3 bound = vec3{size, size, size} * scale;
    vec3 invDir = {1.0f/rayDir.x, 1.0f/rayDir.y, 1.0f/rayDir.z};
    vec3 tMin = (-bound - rayOrigin) * invDir;
    vec3 tMax = (bound - rayOrigin) * invDir;
    vec3 t1 = {std::min(tMin.x, tMax.x), std::min(tMin.y, tMax.y), std::min(tMin.z, tMax.z)};
    vec3 t2 = {std::max(tMin.x, tMax.x), std::max(tMin.y, tMax.y), std::max(tMin.z, tMax.z)};
    float tN = std::max(std::max(t1.x, t1.y), t1.z);
    float tF = std::min(std::min(t2.x, t2.y), t2.z);
    if (tN > tF || tF < 0.0f) return -1.0f;
    return tN;
}

float intersectEllipsoid(Ray r, vec3 pos, vec3 rot, vec3 scale, float radius) {
    float Mat[9], InvMat[9];
    createRotationMatrix3x3(rot, Mat);
    transpose(Mat, InvMat);
    vec3 ro = mul(InvMat, r.origin - pos);
    vec3 rd = mul(InvMat, r.dir);
    ro = {ro.x/scale.x, ro.y/scale.y, ro.z/scale.z};
    rd = {rd.x/scale.x, rd.y/scale.y, rd.z/scale.z};
    vec3 oc = ro;
    float a = dot(rd, rd);
    float b = 2.0f * dot(oc, rd);
    float c = dot(oc, oc) - radius*radius;
    float h = b*b - 4.0f*a*c;
    if(h < 0.0f) return -1.0f;
    return (-b - std::sqrt(h)) / (2.0f*a);
}

float intersectRing(Ray r, vec3 pos, int axis, float radius, float thick) {
    vec3 n = {0,0,0};
    if (axis == 0) n = {1,0,0}; 
    if (axis == 1) n = {0,1,0}; 
    if (axis == 2) n = {0,0,1}; 
    float denom = dot(n, r.dir);
    if (std::abs(denom) < 1e-6) return 10000.0f;
    float t = dot(pos - r.origin, n) / denom;
    if (t < 0.0f) return 10000.0f;
    vec3 p = r.origin + r.dir * t;
    float d = length(p - pos);
    if (d > (radius - thick) && d < (radius + thick)) return t;
    return 10000.0f;
}

// =================================================================
// --- Input Logic ---
// =================================================================
GizmoAxis checkGizmoIntersection(Ray ray, vec3 pivotPos, vec3 camPos) {
    if (!showGizmos) return AXIS_NONE;
    
    float distToCam = length(camPos - pivotPos);
    float scale = distToCam * 0.12f; 
    float minD = 10000.0f;
    GizmoAxis hit = AXIS_NONE;

    if (currentMode == MODE_MOVE || currentMode == MODE_SCALE) {
        if (intersectSphere(ray, pivotPos, 0.3f * scale) > 0.0f) return AXIS_CENTER;
        float totalLen = 1.3f * scale; float hitThick = 0.25f * scale; 
        float bx = intersectBox(ray, pivotPos + vec3{totalLen/2.0f, 0, 0}, {totalLen/2.0f, hitThick, hitThick});
        float by = intersectBox(ray, pivotPos + vec3{0, totalLen/2.0f, 0}, {hitThick, totalLen/2.0f, hitThick});
        float bz = intersectBox(ray, pivotPos + vec3{0, 0, totalLen/2.0f}, {hitThick, hitThick, totalLen/2.0f});
        if (bx > 0 && bx < minD) { minD = bx; hit = AXIS_X; }
        if (by > 0 && by < minD) { minD = by; hit = AXIS_Y; }
        if (bz > 0 && bz < minD) { minD = bz; hit = AXIS_Z; }
    }
    else if (currentMode == MODE_ROTATE) {
        float r = 1.0f * scale; float thick = 0.2f * scale;
        float dX = intersectRing(ray, pivotPos, 0, r, thick); 
        float dY = intersectRing(ray, pivotPos, 2, r, thick); 
        float dZ = intersectRing(ray, pivotPos, 1, r, thick); 
        if (dX < minD) { minD = dX; hit = AXIS_X; }
        if (dY < minD) { minD = dY; hit = AXIS_Y; }
        if (dZ < minD) { minD = dZ; hit = AXIS_Z; }
    }
    return hit;
}

float getMouseDeltaOnAxis(vec3 axisWorldDir, float dx, float dy) {
    CameraBasis b = getCameraBasis();
    vec2 axisScreenDir;
    axisScreenDir.x = dot(axisWorldDir, b.right);
    axisScreenDir.y = dot(axisWorldDir, b.up); 
    float len = std::sqrt(axisScreenDir.x*axisScreenDir.x + axisScreenDir.y*axisScreenDir.y);
    if (len < 0.001f) return 0.0f;
    axisScreenDir.x /= len; axisScreenDir.y /= len;
    return (dx * axisScreenDir.x + (-dy) * axisScreenDir.y);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        cam.leftMouse = false; cam.rightMouse = false; cam.middleMouse = false;
        isDraggingGizmo = false; isBoxSelecting = false;
        return;
    }

    if (action == GLFW_PRESS) {
        double x, y; glfwGetCursorPos(window, &x, &y);
        cam.lastX = x; cam.lastY = y;
        mousePressX = x; mousePressY = y;
        isMouseDragging = false;
    }
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT) cam.rightMouse = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) cam.middleMouse = (action == GLFW_PRESS);
    
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        cam.leftMouse = (action == GLFW_PRESS);
        
        if (action == GLFW_PRESS) {
            double x, y; glfwGetCursorPos(window, &x, &y);
            int w, h; glfwGetFramebufferSize(window, &w, &h);
            Ray ray = getRayFromMouse((float)x, (float)y, (float)w, (float)h);
            
            currentGizmoAxis = AXIS_NONE;
            if (!selectedIndices.empty()) {
                vec3 pivot = getSelectionCenter();
                CameraBasis b = getCameraBasis();
                currentGizmoAxis = checkGizmoIntersection(ray, pivot, b.pos);
                if (currentGizmoAxis != AXIS_NONE) { 
                    isDraggingGizmo = true; 
                    saveUndo(); 
                    return; 
                }
            }
        }
        
        if (action == GLFW_RELEASE) {
            double x, y; glfwGetCursorPos(window, &x, &y);
            int w, h; glfwGetFramebufferSize(window, &w, &h);

            if (isBoxSelecting) {
                if (!(mods & GLFW_MOD_SHIFT)) selectedIndices.clear(); 
                float minX = std::min((float)mousePressX, (float)x);
                float maxX = std::max((float)mousePressX, (float)x);
                float minY = std::min((float)mousePressY, (float)y);
                float maxY = std::max((float)mousePressY, (float)y);
                for(int i=0; i<sceneObjects.size(); i++) {
                    vec3 p = {sceneObjects[i].position.x, sceneObjects[i].position.y, sceneObjects[i].position.z};
                    vec2 screenPos;
                    if (projectWorldToScreen(p, (float)w, (float)h, screenPos)) {
                        if (screenPos.x >= minX && screenPos.x <= maxX && screenPos.y >= minY && screenPos.y <= maxY) {
                            if (std::find(selectedIndices.begin(), selectedIndices.end(), i) == selectedIndices.end()) {
                                selectedIndices.push_back(i);
                            }
                        }
                    }
                }
            }
            else if (!isDraggingGizmo && !isMouseDragging) {
                Ray ray = getRayFromMouse((float)x, (float)y, (float)w, (float)h);
                float minD = 10000.0f;
                int hitIndex = -1;
                for(int i=0; i<sceneObjects.size(); i++) {
                    vec3 p = {sceneObjects[i].position.x, sceneObjects[i].position.y, sceneObjects[i].position.z};
                    vec3 rot = {sceneObjects[i].rotationEuler.x, sceneObjects[i].rotationEuler.y, sceneObjects[i].rotationEuler.z};
                    vec3 scl = {sceneObjects[i].scale.x, sceneObjects[i].scale.y, sceneObjects[i].scale.z};
                    float size = sceneObjects[i].params.x;
                    int type = (int)sceneObjects[i].position.w;
                    float d = -1.0f;
                    if (type == 0) d = intersectEllipsoid(ray, p, rot, scl, size);
                    else if (type == 1) d = intersectBoxOBB(ray, p, rot, scl, size);
                    if (d > 0.0f && d < minD) { minD = d; hitIndex = i; }
                }
                
                if (!(mods & GLFW_MOD_SHIFT)) selectedIndices.clear();

                if (hitIndex != -1) {
                    auto it = std::find(selectedIndices.begin(), selectedIndices.end(), hitIndex);
                    if (it != selectedIndices.end() && (mods & GLFW_MOD_SHIFT)) {
                        selectedIndices.erase(it);
                    } else if (it == selectedIndices.end()) {
                        selectedIndices.push_back(hitIndex);
                    }
                }
            }

            isDraggingGizmo = false;
            isBoxSelecting = false;
            isMouseDragging = false;
            currentGizmoAxis = AXIS_NONE;
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE && !isMouseDragging) {
        if (!selectedIndices.empty()) triggerSceneContextMenu = true;
    }
}

void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    currentMouseX = xpos; currentMouseY = ypos;

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
        if (!cam.leftMouse && !cam.rightMouse && !cam.middleMouse) return;
    }

    float dx = (float)(xpos - cam.lastX);
    float dy = (float)(ypos - cam.lastY);
    cam.lastX = xpos; cam.lastY = ypos;

    if (cam.leftMouse || cam.rightMouse || cam.middleMouse) {
        double dist = std::sqrt(std::pow(xpos - mousePressX, 2) + std::pow(ypos - mousePressY, 2));
        if (dist > DRAG_THRESHOLD) isMouseDragging = true;
    }

    if (cam.leftMouse && isMouseDragging && !isDraggingGizmo) isBoxSelecting = true;

    if (cam.middleMouse || (cam.rightMouse && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)) {
         CameraBasis b = getCameraBasis();
         float s = cam.radius * 0.002f;
         cam.target = cam.target - (b.right * dx * s) + (b.up * dy * s);
         return; 
    }
    if (cam.rightMouse && isMouseDragging) { 
        cam.theta -= dx * 0.005f; cam.phi -= dy * 0.005f; 
    }

    if (cam.leftMouse && isDraggingGizmo && !selectedIndices.empty()) {
        CameraBasis b = getCameraBasis();
        vec3 pivot = getSelectionCenter(); 
        
        if (currentMode == MODE_MOVE) {
            float dist = length(b.pos - pivot);
            float speed = dist * 0.002f; 
            vec3 moveVector = {0,0,0};

            if (currentGizmoAxis == AXIS_CENTER) {
                moveVector = (b.right * dx * speed) + (b.up * (-dy) * speed);
            } else {
                vec3 axis = {0,0,0};
                if (currentGizmoAxis == AXIS_X) axis = {1,0,0}; 
                if (currentGizmoAxis == AXIS_Y) axis = {0,1,0}; 
                if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};
                float delta = getMouseDeltaOnAxis(axis, dx, dy);
                moveVector = axis * delta * speed;
            }
            
            for(int idx : selectedIndices) {
                sceneObjects[idx].position.x += moveVector.x;
                sceneObjects[idx].position.y += moveVector.y;
                sceneObjects[idx].position.z += moveVector.z;
            }
        }
        else if (currentMode == MODE_ROTATE) {
            float delta = (dx + dy) * 0.01f;
            vec3 axis = {0,0,0};
            if (currentGizmoAxis == AXIS_X) axis = {1,0,0};
            if (currentGizmoAxis == AXIS_Y) axis = {0,1,0};
            if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};

            for(int idx : selectedIndices) {
                vec3 currentPos = {sceneObjects[idx].position.x, sceneObjects[idx].position.y, sceneObjects[idx].position.z};
                vec3 relPos = currentPos - pivot;
                vec3 rotatedRel = rotateVector(relPos, axis, delta);
                vec3 newPos = pivot + rotatedRel;
                sceneObjects[idx].position.x = newPos.x;
                sceneObjects[idx].position.y = newPos.y;
                sceneObjects[idx].position.z = newPos.z;

                if (currentGizmoAxis == AXIS_X) sceneObjects[idx].rotationEuler.x += delta;
                if (currentGizmoAxis == AXIS_Y) sceneObjects[idx].rotationEuler.y += delta;
                if (currentGizmoAxis == AXIS_Z) sceneObjects[idx].rotationEuler.z += delta;

                vec3 r = {sceneObjects[idx].rotationEuler.x, sceneObjects[idx].rotationEuler.y, sceneObjects[idx].rotationEuler.z};
                sceneObjects[idx].invRotation = createInverseRotationMatrix4x4(r);
            }
        }
        else if (currentMode == MODE_SCALE) {
            float scaleSpeed = 0.002f; 
            float deltaS = 0.0f;
            vec3 axis = {0,0,0};
            
            if (currentGizmoAxis == AXIS_CENTER) {
                deltaS = (dx - dy) * scaleSpeed;
            } else {
                if (currentGizmoAxis == AXIS_X) axis = {1,0,0}; 
                if (currentGizmoAxis == AXIS_Y) axis = {0,1,0}; 
                if (currentGizmoAxis == AXIS_Z) axis = {0,0,1};
                deltaS = getMouseDeltaOnAxis(axis, dx, dy) * 0.05f;
            }

            for(int idx : selectedIndices) {
                float s = 1.0f + deltaS;
                if(selectedIndices.size() > 1) {
                    vec3 currentPos = {sceneObjects[idx].position.x, sceneObjects[idx].position.y, sceneObjects[idx].position.z};
                    vec3 relPos = currentPos - pivot;
                    vec3 newRel = relPos; 
                    if (currentGizmoAxis == AXIS_CENTER) newRel = relPos * s;
                    else {
                        newRel.x += relPos.x * axis.x * deltaS;
                        newRel.y += relPos.y * axis.y * deltaS;
                        newRel.z += relPos.z * axis.z * deltaS;
                    }
                    vec3 newPos = pivot + newRel;
                    sceneObjects[idx].position.x = newPos.x;
                    sceneObjects[idx].position.y = newPos.y;
                    sceneObjects[idx].position.z = newPos.z;
                }
                if (currentGizmoAxis == AXIS_CENTER) {
                    sceneObjects[idx].scale.x *= s; sceneObjects[idx].scale.y *= s; sceneObjects[idx].scale.z *= s;
                } else {
                    sceneObjects[idx].scale.x += axis.x * deltaS;
                    sceneObjects[idx].scale.y += axis.y * deltaS;
                    sceneObjects[idx].scale.z += axis.z * deltaS;
                }
            }
        }
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) return;
    if (yoffset > 0) cam.radius *= 0.9f; else cam.radius *= 1.1f;
    if (cam.radius < 0.1f) cam.radius = 0.1f;
}

void deleteSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::sort(selectedIndices.rbegin(), selectedIndices.rend());
    for (int idx : selectedIndices) {
        if (idx < sceneObjects.size()) {
            sceneObjects.erase(sceneObjects.begin() + idx);
        }
    }
    selectedIndices.clear();
    isDraggingGizmo = false;
}

void duplicateSelectedObject() {
    saveUndo();
    std::vector<int> newIndices;
    int currentSize = (int)sceneObjects.size();
    for (int idx : selectedIndices) {
        if (sceneObjects.size() >= MAX_OBJECTS) break;
        ObjectData newObj = sceneObjects[idx];
        newObj.position.x += 0.5f; 
        sceneObjects.push_back(newObj);
        newIndices.push_back(currentSize++);
    }
    selectedIndices = newIndices;
}

void processInput(GLFWwindow* window, float deltaTime) {
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard) return;

    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) currentMode = MODE_MOVE;
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) currentMode = MODE_ROTATE;
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) currentMode = MODE_SCALE;
    
    if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !selectedIndices.empty()) {
        cam.target = getSelectionCenter();
    }

    static bool zPressed = false;
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS && (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)) {
        if (!zPressed) { performUndo(); zPressed = true; }
    } else zPressed = false;

    static bool delPressed = false;
    if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) {
        if (!delPressed) { deleteSelectedObject(); delPressed = true; }
    } else delPressed = false;

    static bool dPressed = false;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS && (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)) {
        if (!dPressed) { duplicateSelectedObject(); dPressed = true; }
    } else dPressed = false;

    float speed = 5.0f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 3.0f;
    CameraBasis b = getCameraBasis();
    vec3 forward = normalize(vec3{b.fwd.x, 0.0f, b.fwd.z});
    vec3 right   = normalize(vec3{b.right.x, 0.0f, b.right.z});
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cam.target += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cam.target -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cam.target -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cam.target += right * speed;
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename + ". Make sure you compiled the shader!");
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();
    return buf;
}

// =================================================================
// --- Vulkan Engine Class ---
// =================================================================
class Engine {
public:
    void run() { 
        #ifdef _WIN32
        CoInitialize(NULL); // Инициализация COM для диалогов
        #endif
        initScene(); 
        initWindow(); 
        initVulkan(); 
        initImGui(); 
        mainLoop(); 
        cleanup(); 
        #ifdef _WIN32
        CoUninitialize();
        #endif
    }
    bool framebufferResized = false; 

private:
    GLFWwindow* window;
    VkInstance instance; VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; VkDevice device;
    VkQueue computeQueue, presentQueue; VkSurfaceKHR surface; VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages; std::vector<VkImageView> swapchainImageViews;
    VkExtent2D swapchainExtent;
    VkPipelineLayout pipelineLayout; VkPipeline computePipeline;
    VkDescriptorSetLayout descriptorSetLayout; VkDescriptorPool descriptorPool; std::vector<VkDescriptorSet> descriptorSets;
    VkCommandPool commandPool; std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable, renderFinished; std::vector<VkFence> inFlightFences;
    fs::file_time_type lastShaderWriteTime;
    VkBuffer sceneBuffer; VkDeviceMemory sceneBufferMemory; void* sceneBufferMapped = nullptr;

    VkDescriptorPool imguiDescriptorPool;
    VkRenderPass imguiRenderPass;
    std::vector<VkFramebuffer> imguiFramebuffers;
    vec4 currentUiBounds = {0,0,0,0}; 

    // --- NOS File State ---
    std::string currentLoadedFile = "Untitled";
    NosHeader lastLoadedHeader = {};

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void resetScene() {
        sceneObjects.clear();
        selectedIndices.clear();
        undoStack.clear();
        initScene();
        currentLoadedFile = "Untitled";
    }

    void initScene() {
        mat4 identity; 
        memset(&identity, 0, sizeof(mat4)); 
        identity.m[0]=1; identity.m[5]=1; identity.m[10]=1; identity.m[15]=1;
        sceneObjects.push_back({identity, {0, 0.5f, 0, 0.0f}, {1,1,1,0}, {0.8f, 0.0f, 0.2f, 1.0f}, {1.0f, 0.8f, 0.1f, 0.2f}, {0,0,0,0}});
        sceneObjects.push_back({identity, {0.6f, 0.8f, 0.4f, 1.0f}, {0.8f, 0.8f, 0.8f, 0}, {0.6f, 1.0f, 0.1f, 0.0f}, {0.2f, 0.4f, 1.0f, 0.8f}, {0.5f, 0.5f, 0.0f, 0}});
        sceneObjects.push_back({identity, {0, -1.0f, 0, 1.0f}, {6, 0.1f, 6, 0}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.1f, 0.1f, 0.15f, 0.9f}, {0,0,0,0}});
    }

    void initWindow() {
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); 
        window = glfwCreateWindow(INITIAL_WIDTH, INITIAL_HEIGHT, "SDF Engine", nullptr, nullptr);
        if (!window) throw std::runtime_error("Failed to create window");
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetCursorPosCallback(window, cursor_pos_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetScrollCallback(window, scroll_callback);
    }
    
    uint32_t findMemoryType(uint32_t f, VkMemoryPropertyFlags p) { VkPhysicalDeviceMemoryProperties m; vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m); for(uint32_t i=0; i<m.memoryTypeCount; i++) if((f&(1<<i))&&(m.memoryTypes[i].propertyFlags&p)==p) return i; throw std::runtime_error("mem"); }
    void createBuffer(VkDeviceSize s, VkBufferUsageFlags u, VkMemoryPropertyFlags p, VkBuffer& b, VkDeviceMemory& m) { VkBufferCreateInfo i{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; i.size=s; i.usage=u; vkCreateBuffer(device,&i,nullptr,&b); VkMemoryRequirements r; vkGetBufferMemoryRequirements(device,b,&r); VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=r.size; a.memoryTypeIndex=findMemoryType(r.memoryTypeBits,p); vkAllocateMemory(device,&a,nullptr,&m); vkBindBufferMemory(device,b,m,0); }
    
    // --- VULKAN SCREENSHOT CAPTURE ---
    std::vector<uint8_t> captureScreenshot() {
        VkDeviceSize imageSize = swapchainExtent.width * swapchainExtent.height * 4;
        VkBuffer dstBuffer;
        VkDeviceMemory dstBufferMemory;

        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     dstBuffer, dstBufferMemory);

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages[0]; 
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {swapchainExtent.width, swapchainExtent.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, swapchainImages[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(computeQueue);

        std::vector<uint8_t> pixels(imageSize);
        void* data;
        vkMapMemory(device, dstBufferMemory, 0, imageSize, 0, &data);
        memcpy(pixels.data(), data, (size_t)imageSize);
        vkUnmapMemory(device, dstBufferMemory);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkDestroyBuffer(device, dstBuffer, nullptr);
        vkFreeMemory(device, dstBufferMemory, nullptr);

        // Swizzle BGR -> RGB
        for(size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i+2]); 
        }
        return pixels;
    }

    // Helper to downsample image data to 256x144 for thumbnails
    std::vector<uint8_t> downsamplePixels(const std::vector<uint8_t>& srcPixels, int srcW, int srcH, int dstW, int dstH) {
        std::vector<uint8_t> outPixels(dstW * dstH * 4); // RGBA
        float xRatio = ((float)(srcW)) / dstW;
        float yRatio = ((float)(srcH)) / dstH;

        for (int i = 0; i < dstH; i++) {
            for (int j = 0; j < dstW; j++) {
                int px = (int)(j * xRatio);
                int py = (int)(i * yRatio);
                int srcIdx = (py * srcW + px) * 4;
                int dstIdx = (i * dstW + j) * 4;
                
                if (srcIdx < srcPixels.size() - 4) {
                    outPixels[dstIdx + 0] = srcPixels[srcIdx + 0]; // R
                    outPixels[dstIdx + 1] = srcPixels[srcIdx + 1]; // G
                    outPixels[dstIdx + 2] = srcPixels[srcIdx + 2]; // B
                    outPixels[dstIdx + 3] = 255;                   // Alpha
                }
            }
        }
        return outPixels;
    }

    // --- NOS SERIALIZATION ---
    void saveNos(const std::string& path) {
        vkDeviceWaitIdle(device); 

        // Capture full resolution
        std::vector<uint8_t> fullRes = captureScreenshot();
        
        // Downsample to 256x144 (Size Optimization)
        std::vector<uint8_t> thumbnail = downsamplePixels(fullRes, swapchainExtent.width, swapchainExtent.height, 256, 144);

        NosHeader header;
        strncpy(header.magic, "NOS!", 4);
        header.version = 100;
        header.objectCount = (uint32_t)sceneObjects.size();
        header.checksum = calculateChecksum(sceneObjects);
        header.thumbnailOffset = sizeof(NosHeader) + sizeof(SceneMetadata) + (sceneObjects.size() * sizeof(ObjectData));
        header.thumbnailSize = (uint32_t)thumbnail.size();
        memset(header.reserved, 0, sizeof(header.reserved));

        SceneMetadata meta;
        meta.target = cam.target;
        meta.theta = cam.theta;
        meta.phi = cam.phi;
        meta.radius = cam.radius;
        memset(meta.padding, 0, sizeof(meta.padding));

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to save file: " << path << std::endl;
            return;
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(NosHeader));
        file.write(reinterpret_cast<const char*>(&meta), sizeof(SceneMetadata));
        file.write(reinterpret_cast<const char*>(sceneObjects.data()), sceneObjects.size() * sizeof(ObjectData));
        file.write(reinterpret_cast<const char*>(thumbnail.data()), thumbnail.size());
        
        file.close();
        currentLoadedFile = path;
        std::cout << "[NOS] Saved to " << path << " (Size optimized ~" << (thumbnail.size()/1024) << "KB)" << std::endl;
    }

    void loadNos(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << path << std::endl;
            return;
        }

        NosHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(NosHeader));

        if (strncmp(header.magic, "NOS!", 4) != 0) {
            std::cerr << "Invalid file format (Magic mismatch)" << std::endl;
            return;
        }

        SceneMetadata meta;
        file.read(reinterpret_cast<char*>(&meta), sizeof(SceneMetadata));

        std::vector<ObjectData> loadedObjects(header.objectCount);
        file.read(reinterpret_cast<char*>(loadedObjects.data()), header.objectCount * sizeof(ObjectData));

        uint32_t calcSum = calculateChecksum(loadedObjects);
        if (calcSum != header.checksum) {
            std::cerr << "Checksum mismatch! File corrupted." << std::endl;
        }

        sceneObjects = loadedObjects;
        cam.target = meta.target;
        cam.theta = meta.theta;
        cam.phi = meta.phi;
        cam.radius = meta.radius;

        updateObjectMatrices();
        selectedIndices.clear();
        undoStack.clear(); 
        currentLoadedFile = path;
        lastLoadedHeader = header;
        
        file.close();
        std::cout << "[NOS] Loaded from " << path << std::endl;
    }

    void createSSBO() { createBuffer(sizeof(ObjectData)*MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sceneBuffer, sceneBufferMemory); vkMapMemory(device, sceneBufferMemory, 0, sizeof(ObjectData)*MAX_OBJECTS, 0, &sceneBufferMapped); }
    
    void updateObjectMatrices() {
        for(auto& obj : sceneObjects) {
            vec3 rot = {obj.rotationEuler.x, obj.rotationEuler.y, obj.rotationEuler.z};
            obj.invRotation = createInverseRotationMatrix4x4(rot);
        }
    }
    
    void updateSSBO() { 
        if (!sceneObjects.empty()) {
            updateObjectMatrices();
            memcpy(sceneBufferMapped, sceneObjects.data(), sceneObjects.size() * sizeof(ObjectData)); 
        }
    }
    
    void initVulkan() { 
        createInstance(); createSurface(); pickPhysicalDevice(); createLogicalDevice(); createSwapchain(); createImageViews(); 
        createSSBO(); createDescriptors(); createPipeline(); 
        if(fs::exists(SHADER_SOURCE)) lastShaderWriteTime = fs::last_write_time(SHADER_SOURCE); 
        createCommandPool(); createCommandBuffers(); createSyncObjects(); 
    }

    void setupImGuiStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        
        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 3.0f;
        style.GrabRounding      = 2.0f;
        style.PopupRounding     = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.TabRounding       = 2.0f;
        style.WindowPadding     = ImVec2(10, 10);
        style.FramePadding      = ImVec2(6, 4);
        style.ItemSpacing       = ImVec2(6, 6);
        style.WindowBorderSize  = 0.0f; 
        style.FrameBorderSize   = 1.0f; 

        ImVec4* colors = style.Colors;
        ImVec4 greyDark     = ImVec4(0.11f, 0.11f, 0.11f, guiAlpha);
        ImVec4 greyMedium   = ImVec4(0.18f, 0.18f, 0.18f, guiAlpha);
        ImVec4 greyLight    = ImVec4(0.26f, 0.26f, 0.26f, guiAlpha);
        ImVec4 bgAlpha      = ImVec4(0.11f, 0.11f, 0.11f, guiAlpha);
        ImVec4 textWhite    = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        ImVec4 accentBlue   = ImVec4(0.28f, 0.56f, 0.90f, 1.00f); 
        ImVec4 accentHover  = ImVec4(0.38f, 0.66f, 1.00f, 1.00f);

        colors[ImGuiCol_WindowBg]       = bgAlpha;
        colors[ImGuiCol_ChildBg]        = bgAlpha;
        colors[ImGuiCol_PopupBg]        = ImVec4(0.15f, 0.15f, 0.15f, 0.95f);
        colors[ImGuiCol_Border]         = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_FrameBg]        = greyMedium;
        colors[ImGuiCol_FrameBgHovered] = greyLight;
        colors[ImGuiCol_FrameBgActive]  = greyLight;
        colors[ImGuiCol_TitleBg]        = greyDark;
        colors[ImGuiCol_TitleBgActive]  = greyDark;
        colors[ImGuiCol_MenuBarBg]      = greyDark;
        colors[ImGuiCol_Button]         = greyMedium;
        colors[ImGuiCol_ButtonHovered]  = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        colors[ImGuiCol_ButtonActive]   = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
        colors[ImGuiCol_Header]         = greyMedium;
        colors[ImGuiCol_HeaderHovered]  = accentHover;
        colors[ImGuiCol_HeaderActive]   = accentBlue;
        colors[ImGuiCol_CheckMark]      = accentBlue;
        colors[ImGuiCol_SliderGrab]     = accentBlue;
        colors[ImGuiCol_SliderGrabActive] = textWhite;
        colors[ImGuiCol_Text]           = textWhite;
        colors[ImGuiCol_TextSelectedBg] = accentBlue;
    }
    
    void initImGui() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiDescriptorPool);

        createGuiRenderPass();
        createGuiFramebuffers();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        setupImGuiStyle();

        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance;
        init_info.PhysicalDevice = physicalDevice;
        init_info.Device = device;
        init_info.QueueFamily = 0; 
        init_info.Queue = presentQueue; 
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = imguiDescriptorPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = (uint32_t)swapchainImages.size();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        ImGui_ImplVulkan_Init(&init_info, imguiRenderPass);

        VkCommandBuffer command_buffer = commandBuffers[0];
        vkResetCommandBuffer(command_buffer, 0);
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &begin_info);
        ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
        vkEndCommandBuffer(command_buffer);
        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        vkQueueSubmit(computeQueue, 1, &submit_info, VK_NULL_HANDLE);
        vkDeviceWaitIdle(device);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    void createGuiRenderPass() {
        VkAttachmentDescription attachment = {};
        attachment.format = VK_FORMAT_B8G8R8A8_UNORM; 
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; 
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; 
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        if (vkCreateRenderPass(device, &info, nullptr, &imguiRenderPass) != VK_SUCCESS) throw std::runtime_error("failed to create render pass!");
    }

    void createGuiFramebuffers() {
        imguiFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView attachments[] = { swapchainImageViews[i] };
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = imguiRenderPass;
            info.attachmentCount = 1;
            info.pAttachments = attachments;
            info.width = swapchainExtent.width;
            info.height = swapchainExtent.height;
            info.layers = 1;
            if (vkCreateFramebuffer(device, &info, nullptr, &imguiFramebuffers[i]) != VK_SUCCESS) throw std::runtime_error("failed to create framebuffer!");
        }
    }

    void createInstance() { VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"SDF",1,"",1,VK_API_VERSION_1_3}; uint32_t c; const char** e=glfwGetRequiredInstanceExtensions(&c); VkInstanceCreateInfo i{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&a,0,nullptr,c,e}; vkCreateInstance(&i,nullptr,&instance); }
    void createSurface() { glfwCreateWindowSurface(instance,window,nullptr,&surface); }
    void pickPhysicalDevice() { uint32_t c; vkEnumeratePhysicalDevices(instance,&c,nullptr); std::vector<VkPhysicalDevice> d(c); vkEnumeratePhysicalDevices(instance,&c,d.data()); physicalDevice=d[0]; }
    void createLogicalDevice() { float p=1.0f; VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,nullptr,0,0,1,&p}; const char* e[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME}; VkDeviceCreateInfo i{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,nullptr,0,1,&q,0,nullptr,1,e}; vkCreateDevice(physicalDevice,&i,nullptr,&device); vkGetDeviceQueue(device,0,0,&computeQueue); vkGetDeviceQueue(device,0,0,&presentQueue); }
    void createSwapchain() { VkSurfaceCapabilitiesKHR c; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,surface,&c); if(c.currentExtent.width!=UINT32_MAX) swapchainExtent=c.currentExtent; else { int w,h; glfwGetFramebufferSize(window,&w,&h); swapchainExtent={static_cast<uint32_t>(w),static_cast<uint32_t>(h)}; } VkSwapchainCreateInfoKHR i{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; i.surface=surface; i.minImageCount=c.minImageCount+1; if(c.maxImageCount>0&&i.minImageCount>c.maxImageCount) i.minImageCount=c.maxImageCount; i.imageFormat=VK_FORMAT_B8G8R8A8_UNORM; i.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; i.imageExtent=swapchainExtent; i.imageArrayLayers=1; i.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT; i.preTransform=c.currentTransform; i.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; i.presentMode=VK_PRESENT_MODE_FIFO_KHR; i.clipped=VK_TRUE; vkCreateSwapchainKHR(device,&i,nullptr,&swapchain); uint32_t n; vkGetSwapchainImagesKHR(device,swapchain,&n,nullptr); swapchainImages.resize(n); vkGetSwapchainImagesKHR(device,swapchain,&n,swapchainImages.data()); }
    void createImageViews() { swapchainImageViews.resize(swapchainImages.size()); for(size_t i=0;i<swapchainImages.size();i++) { VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,nullptr,0,swapchainImages[i],VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_B8G8R8A8_UNORM,{},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; vkCreateImageView(device,&v,nullptr,&swapchainImageViews[i]); } }
    void createDescriptors() { VkDescriptorSetLayoutBinding b[]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT}}; VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,2,b}; vkCreateDescriptorSetLayout(device,&l,nullptr,&descriptorSetLayout); VkDescriptorPoolSize s[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,(uint32_t)swapchainImages.size()},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,(uint32_t)swapchainImages.size()}}; VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,nullptr,0,(uint32_t)swapchainImages.size(),2,s}; vkCreateDescriptorPool(device,&p,nullptr,&descriptorPool); std::vector<VkDescriptorSetLayout> dl(swapchainImages.size(),descriptorSetLayout); VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,descriptorPool,(uint32_t)swapchainImages.size(),dl.data()}; descriptorSets.resize(swapchainImages.size()); vkAllocateDescriptorSets(device,&a,descriptorSets.data()); for(size_t i=0;i<swapchainImages.size();i++) { VkDescriptorImageInfo im{nullptr,swapchainImageViews[i],VK_IMAGE_LAYOUT_GENERAL}; VkDescriptorBufferInfo bu{sceneBuffer,0,sizeof(ObjectData)*MAX_OBJECTS}; VkWriteDescriptorSet w[]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,descriptorSets[i],0,0,1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&im,nullptr,nullptr},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,descriptorSets[i],1,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&bu,nullptr}}; vkUpdateDescriptorSets(device,2,w,0,nullptr); } }
    void createPipeline() { auto c=readFile(SHADER_BINARY); VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,nullptr,0,c.size(),(uint32_t*)c.data()}; VkShaderModule sm; vkCreateShaderModule(device,&m,nullptr,&sm); VkPushConstantRange p{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(PushConstants)}; VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,nullptr,0,1,&descriptorSetLayout,1,&p}; vkCreatePipelineLayout(device,&l,nullptr,&pipelineLayout); VkComputePipelineCreateInfo i{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,nullptr,0,{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,sm,"main"},pipelineLayout}; vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&i,nullptr,&computePipeline); vkDestroyShaderModule(device,sm,nullptr); }
    void createCommandPool() { VkCommandPoolCreateInfo i{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}; vkCreateCommandPool(device,&i,nullptr,&commandPool); }
    void createCommandBuffers() { commandBuffers.resize(swapchainImages.size()); VkCommandBufferAllocateInfo i{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,commandPool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,(uint32_t)commandBuffers.size()}; vkAllocateCommandBuffers(device,&i,commandBuffers.data()); }
    void createSyncObjects() { imageAvailable.resize(2); renderFinished.resize(2); inFlightFences.resize(2); VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,nullptr,VK_FENCE_CREATE_SIGNALED_BIT}; for(int i=0;i<2;i++) { vkCreateSemaphore(device,&s,nullptr,&imageAvailable[i]); vkCreateSemaphore(device,&s,nullptr,&renderFinished[i]); vkCreateFence(device,&f,nullptr,&inFlightFences[i]); } }
    
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t i) {
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bi);
        
        // 1. Compute Barrier
        VkImageMemoryBarrier b1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b1.srcAccessMask = 0;
        b1.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.image = swapchainImages[i];
        b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);
        
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);
        
        PushConstants pc; 
        pc.resolution = { (float)swapchainExtent.width, (float)swapchainExtent.height };
        pc.time=(float)glfwGetTime(); 
        pc.objectCount=(int)sceneObjects.size(); 
        pc.selectedIndex = selectedIndices.empty() ? -1 : selectedIndices.back();
        pc.draggingAxis=(isDraggingGizmo && showGizmos)?currentGizmoAxis:0; 
        pc.mode=currentMode;
        pc.uiMinX = currentUiBounds.x; pc.uiMinY = currentUiBounds.y;
        pc.uiMaxX = currentUiBounds.z; pc.uiMaxY = currentUiBounds.w;
        pc.shadingMode = currentShadingMode; // Pass Shading Mode
        
        updateCameraMatrix(pc.camToWorld);
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDispatch(cb, (swapchainExtent.width+15)/16, (swapchainExtent.height+15)/16, 1);
        
        // 2. ImGui Barrier
        VkImageMemoryBarrier b2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b2.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        b2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b2.image = swapchainImages[i];
        b2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);
        
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = imguiRenderPass;
        info.framebuffer = imguiFramebuffers[i];
        info.renderArea.extent = swapchainExtent;
        info.clearValueCount = 0; 
        
        vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        vkCmdEndRenderPass(cb);
        
        vkEndCommandBuffer(cb);
    }
    
    // --- UI RENDER (Updated with Viewport Shading Header) ---
    void renderUI() {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        setupImGuiStyle();
        ImGuiIO& io = ImGui::GetIO();
        
        float menuHeight = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            menuHeight = ImGui::GetWindowSize().y;
            
            const char* modes[] = { "Object Redactor", "Map Redactor", "Game Build Redactor" };
            static int currentModeIdx = 0;
            ImGui::PushItemWidth(150);
            if (ImGui::Combo("##Mode", &currentModeIdx, modes, IM_ARRAYSIZE(modes))) {
                if (currentModeIdx == 0) currentEditorMode = EDITOR_OBJECT;
                if (currentModeIdx == 1) currentEditorMode = EDITOR_MAP;
                if (currentModeIdx == 2) currentEditorMode = EDITOR_GAME_BUILD;
            }
            ImGui::PopItemWidth();
            ImGui::Separator();

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Reset Scene")) { resetScene(); }
                if (ImGui::MenuItem("Import .Nos")) {
                    std::string path = FileDialogs::OpenFile(window, "NOS Scene File (*.nos)\0*.nos\0All Files (*.*)\0*.*\0");
                    if (!path.empty()) { loadNos(path); }
                }
                if (ImGui::MenuItem("Export .Nos")) {
                    std::string path = FileDialogs::SaveFile(window, "NOS Scene File (*.nos)\0*.nos\0", defaultExportPath);
                    if (!path.empty()) {
                        if (path.find(".nos") == std::string::npos) path += ".nos";
                        saveNos(path);
                    }
                }
                if (ImGui::BeginMenu("Recent Project Info")) {
                    ImGui::Text("File: %s", currentLoadedFile.c_str());
                    if (currentLoadedFile != "Untitled") {
                         ImGui::Text("Version: %d", lastLoadedHeader.version);
                         ImGui::Text("Objects: %d", lastLoadedHeader.objectCount);
                         ImGui::Text("Size: %d bytes", sizeof(NosHeader) + sizeof(SceneMetadata) + lastLoadedHeader.objectCount * sizeof(ObjectData) + lastLoadedHeader.thumbnailSize);
                    } else {
                        ImGui::TextDisabled("No file loaded");
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) { glfwSetWindowShouldClose(window, true); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Properties")) { showSettingsWindow = true; }
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) { performUndo(); }
                ImGui::EndMenu();
            }

            // --- VIEWPORT SHADING BUTTONS (Blender Style) ---
            float buttonWidth = 25.0f;
            float totalWidth = 4 * buttonWidth + 3 * ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetWindowWidth() - totalWidth - 20.0f);
            
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f); 
            
            if (currentShadingMode == 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            if (ImGui::Button("W", ImVec2(buttonWidth, 0))) currentShadingMode = 0;
            if (currentShadingMode == 0) ImGui::PopStyleColor();
            ImGui::SameLine();

            if (currentShadingMode == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            if (ImGui::Button("S", ImVec2(buttonWidth, 0))) currentShadingMode = 1;
            if (currentShadingMode == 1) ImGui::PopStyleColor();
            ImGui::SameLine();

            if (currentShadingMode == 2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            if (ImGui::Button("M", ImVec2(buttonWidth, 0))) currentShadingMode = 2;
            if (currentShadingMode == 2) ImGui::PopStyleColor();
            ImGui::SameLine();

            if (currentShadingMode == 3) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            if (ImGui::Button("R", ImVec2(buttonWidth, 0))) currentShadingMode = 3;
            if (currentShadingMode == 3) ImGui::PopStyleColor();
            
            ImGui::PopStyleVar();

            ImGui::EndMainMenuBar();
        }

        float headerHeight = 28.0f;
        if (showToolSettings) {
            ImGui::SetNextWindowPos(ImVec2(0, menuHeight));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, headerHeight));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, guiAlpha)); 
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 4));
            
            ImGui::Begin("HeaderStrip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
            {
                if(ImGui::Button("View")) { ImGui::OpenPopup("HeaderViewMenu"); }
                if (ImGui::BeginPopup("HeaderViewMenu")) {
                    ImGui::MenuItem("Toolbar", "T", &showToolbar);
                    ImGui::MenuItem("Sidebar", "N", &showSidebar);
                    ImGui::MenuItem("Tool Settings", NULL, &showToolSettings);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if(ImGui::Button("Select")) { ImGui::OpenPopup("SelectMenu"); }
                if (ImGui::BeginPopup("SelectMenu")) {
                    if(ImGui::MenuItem("All", "A")) { selectedIndices.resize(sceneObjects.size()); std::iota(selectedIndices.begin(), selectedIndices.end(), 0); }
                    if(ImGui::MenuItem("None", "Alt+A")) { selectedIndices.clear(); }
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Add")) { ImGui::OpenPopup("AddMenu"); }
                if (ImGui::BeginPopup("AddMenu")) {
                    if (ImGui::BeginMenu("Mesh")) {
                        if (ImGui::MenuItem("Sphere")) { saveUndo(); if (sceneObjects.size() < MAX_OBJECTS) { mat4 id; memset(&id,0,sizeof(id)); id.m[0]=1;id.m[5]=1;id.m[10]=1;id.m[15]=1; sceneObjects.push_back({id, {0,2,0,0}, {1,1,1,0}, {1.0f,0,0,0}, {1,1,1,1}, {0,0,0,0}}); selectedIndices.clear(); selectedIndices.push_back((int)sceneObjects.size() - 1); } }
                        if (ImGui::MenuItem("Cube")) { saveUndo(); if (sceneObjects.size() < MAX_OBJECTS) { mat4 id; memset(&id,0,sizeof(id)); id.m[0]=1;id.m[5]=1;id.m[10]=1;id.m[15]=1; sceneObjects.push_back({id, {0,2,0,1}, {1,1,1,0}, {1.0f,0,0,0}, {1,1,1,1}, {0,0,0,0}}); selectedIndices.clear(); selectedIndices.push_back((int)sceneObjects.size() - 1); } }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if(ImGui::Button("Object")) { ImGui::OpenPopup("ObjectMenu"); } 
                if (ImGui::BeginPopup("ObjectMenu")) {
                    if(ImGui::MenuItem("Duplicate Objects", "Shift+D")) { duplicateSelectedObject(); }
                    if(ImGui::MenuItem("Delete", "X")) { deleteSelectedObject(); }
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        } else { headerHeight = 0.0f; }

        float toolbarWidth = showToolbar ? 50.0f : 0.0f;
        float rightPanelWidth = showSidebar ? 320.0f : 0.0f;
        float margin = 10.0f;
        float topOffset = menuHeight + headerHeight + (showToolSettings ? margin : 0); 
        float workspaceHeight = io.DisplaySize.y - topOffset;

        auto SetWindowLayout = [&](float x, float y, float w, float h) { ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h)); };

        if (showToolbar) {
            SetWindowLayout(0, topOffset, toolbarWidth, workspaceHeight);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, guiAlpha));
            ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
                ImGui::Button("Sel", ImVec2(30, 30)); ImGui::Separator();
                if(currentMode == MODE_MOVE) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if(ImGui::Button("Mov", ImVec2(30, 30))) currentMode = MODE_MOVE;
                if(currentMode == MODE_MOVE) ImGui::PopStyleColor();
                if(currentMode == MODE_ROTATE) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if(ImGui::Button("Rot", ImVec2(30, 30))) currentMode = MODE_ROTATE;
                if(currentMode == MODE_ROTATE) ImGui::PopStyleColor();
                if(currentMode == MODE_SCALE) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if(ImGui::Button("Scl", ImVec2(30, 30))) currentMode = MODE_SCALE;
                if(currentMode == MODE_SCALE) ImGui::PopStyleColor();
                ImGui::Separator();
                if (showGizmos) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button("Giz", ImVec2(30, 30))) showGizmos = !showGizmos;
                if (showGizmos) ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }

        float rightX = io.DisplaySize.x - rightPanelWidth - margin;
        if (showSidebar) {
            float hierarchyHeight = workspaceHeight * 0.4f - margin * 1.5f; 
            float inspectorHeight = workspaceHeight * 0.6f - margin * 1.5f;

            SetWindowLayout(rightX, topOffset, rightPanelWidth, hierarchyHeight);
            ImGui::Begin("Scene Collection", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
            {
                ImGui::TextDisabled("HIERARCHY"); ImGui::Separator();
                ImGui::BeginChild("ScrollH", ImVec2(0,0), false);
                for (int i = 0; i < sceneObjects.size(); i++) {
                    std::string label = ((int)sceneObjects[i].position.w == 0 ? "Sphere " : "Box ") + std::to_string(i);
                    bool isSelected = false;
                    for(int s : selectedIndices) if(s == i) isSelected = true;
                    if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28f, 0.56f, 0.90f, 1.00f));
                    if (ImGui::Selectable(label.c_str(), isSelected)) { if(!ImGui::GetIO().KeyShift) selectedIndices.clear(); selectedIndices.push_back(i); }
                    if (isSelected) ImGui::PopStyleColor();
                }
                ImGui::EndChild();
            }
            ImGui::End();

            SetWindowLayout(rightX, topOffset + hierarchyHeight + margin * 2, rightPanelWidth, inspectorHeight);
            ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
            {
                if (!selectedIndices.empty()) {
                    int activeIdx = selectedIndices.back();
                    ObjectData& obj = sceneObjects[activeIdx];
                    ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.00f), "Active Object %d", activeIdx);
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Dummy(ImVec2(0, 5));
                        ImGui::Text("Position"); ImGui::DragFloat3("##Pos", &obj.position.x, 0.05f);
                        ImGui::Text("Rotation");
                        if (ImGui::DragFloat3("##Rot", &obj.rotationEuler.x, 0.05f)) { vec3 r = {obj.rotationEuler.x, obj.rotationEuler.y, obj.rotationEuler.z}; obj.invRotation = createInverseRotationMatrix4x4(r); }
                        ImGui::Text("Scale"); ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f);
                    }
                    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::ColorEdit3("Color", &obj.color.x);
                        ImGui::SliderFloat("Metallic", &obj.params.w, 0.0f, 1.0f);
                        ImGui::SliderFloat("Roughness", &obj.color.w, 0.0f, 1.0f);
                    }
                    if (ImGui::Button("DELETE", ImVec2(-1, 0))) { deleteSelectedObject(); }
                } else { ImGui::TextDisabled("Select an object."); }
            }
            ImGui::End();
        }

        if (isBoxSelecting) {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            ImVec2 p1(std::min((float)mousePressX, (float)currentMouseX), std::min((float)mousePressY, (float)currentMouseY));
            ImVec2 p2(std::max((float)mousePressX, (float)currentMouseX), std::max((float)mousePressY, (float)currentMouseY));
            drawList->AddRect(p1, p2, IM_COL32(255, 255, 255, 150), 0.0f, 0, 1.0f); 
            drawList->AddRectFilled(p1, p2, IM_COL32(255, 255, 255, 30)); 
        }

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        for (int idx : selectedIndices) {
            if (idx >= sceneObjects.size()) continue;
            vec3 pos = {sceneObjects[idx].position.x, sceneObjects[idx].position.y, sceneObjects[idx].position.z};
            float size = sceneObjects[idx].params.x; 
            vec3 corners[8]; float sx = size * sceneObjects[idx].scale.x; float sy = size * sceneObjects[idx].scale.y; float sz = size * sceneObjects[idx].scale.z;
            for(int i=0; i<8; i++) { vec3 offset = { (i&1?sx:-sx), (i&2?sy:-sy), (i&4?sz:-sz) }; corners[i] = pos + offset; }
            ImVec2 minS = {10000, 10000}, maxS = {-10000, -10000}; bool visible = false;
            for(int i=0; i<8; i++) { vec2 sPos; if(projectWorldToScreen(corners[i], (float)w, (float)h, sPos)) { minS.x = std::min(minS.x, sPos.x); minS.y = std::min(minS.y, sPos.y); maxS.x = std::max(maxS.x, sPos.x); maxS.y = std::max(maxS.y, sPos.y); visible = true; } }
            if(visible && minS.x < maxS.x) { dl->AddRect(minS, maxS, IM_COL32(255, 140, 0, 200), 0.0f, 0, 2.0f); }
        }

        if (triggerSceneContextMenu) { ImGui::OpenPopup("SceneContextMenu"); triggerSceneContextMenu = false; }
        if (ImGui::BeginPopup("SceneContextMenu")) { if (ImGui::MenuItem("Duplicate")) { duplicateSelectedObject(); } if (ImGui::MenuItem("Delete")) { deleteSelectedObject(); } ImGui::EndPopup(); }

        // --- PROPERTIES (SETTINGS) WINDOW ---
        if (showSettingsWindow) {
            ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Preferences", &showSettingsWindow, ImGuiWindowFlags_NoCollapse)) {
                
                ImGui::BeginChild("Categories", ImVec2(150, 0), true);
                if (ImGui::Selectable("Interface", selectedSettingsTab == 0)) selectedSettingsTab = 0;
                if (ImGui::Selectable("Data", selectedSettingsTab == 1)) selectedSettingsTab = 1; 
                ImGui::EndChild();
                ImGui::SameLine();

                ImGui::BeginGroup();
                ImGui::BeginChild("SettingsContent", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
                
                if (selectedSettingsTab == 0) { 
                    ImGui::Text("User Interface"); 
                    ImGui::Separator(); 
                    if(ImGui::SliderFloat("Menu Transparency", &guiAlpha, 0.1f, 1.0f, "%.2f")) { setupImGuiStyle(); } 
                } 
                else if (selectedSettingsTab == 1) {
                    ImGui::Text("Project Defaults");
                    ImGui::Separator();
                    
                    ImGui::Dummy(ImVec2(0, 10));
                    ImGui::Text("Default Export Location:");
                    
                    std::string dispPath = defaultExportPath.empty() ? "(Not set - using working directory)" : defaultExportPath;
                    ImGui::InputText("##path", (char*)dispPath.c_str(), dispPath.size(), ImGuiInputTextFlags_ReadOnly);
                    ImGui::SameLine();
                    
                    if (ImGui::Button("Browse...")) {
                        std::string newPath = FileDialogs::SelectFolder(window);
                        if (!newPath.empty()) {
                            defaultExportPath = newPath;
                        }
                    }
                    
                    if (!defaultExportPath.empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear")) defaultExportPath = "";
                    }

                    ImGui::TextDisabled("This folder will be opened by default when you save a new project.");
                }

                ImGui::EndChild();
                ImGui::EndGroup();
            }
            ImGui::End();
        }

        currentUiBounds = { showSidebar ? rightX : io.DisplaySize.x, 0.0f, io.DisplaySize.x, io.DisplaySize.y };
        ImGui::Render();
    }

    void reloadShader() { int result = system(("glslc " + SHADER_SOURCE + " -o " + SHADER_BINARY).c_str()); if (result == 0) { vkDeviceWaitIdle(device); vkDestroyPipeline(device, computePipeline, nullptr); try { createPipeline(); std::cout << "[Shader] Reloaded" << std::endl; } catch(...) {} } }
    void cleanupSwapchain() { for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr); for (auto fb : imguiFramebuffers) vkDestroyFramebuffer(device, fb, nullptr); vkDestroySwapchainKHR(device, swapchain, nullptr); vkDestroyDescriptorPool(device, descriptorPool, nullptr); }
    void recreateSwapchain() { int width=0,height=0; glfwGetFramebufferSize(window,&width,&height); while(width==0||height==0){glfwGetFramebufferSize(window,&width,&height); glfwWaitEvents();} vkDeviceWaitIdle(device); cleanupSwapchain(); createSwapchain(); createImageViews(); createGuiFramebuffers(); createDescriptors(); ImGui_ImplVulkan_SetMinImageCount(2); }

    void mainLoop() {
        int currentFrame = 0; double lastTime = glfwGetTime(); int frameCount = 0; double fpsTimer = lastTime;
        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime(); float deltaTime = (float)(currentTime - lastTime); lastTime = currentTime;
            frameCount++;
            if (currentTime - fpsTimer >= 1.0) { std::string modeS = "Move"; if(currentMode==MODE_ROTATE) modeS="Rotate"; if(currentMode==MODE_SCALE) modeS="Scale"; std::string title = "SDF Engine | FPS: " + std::to_string(frameCount); glfwSetWindowTitle(window, title.c_str()); frameCount=0; fpsTimer=currentTime; }
            glfwPollEvents(); 
            processInput(window, deltaTime);
            renderUI();
            if (fs::exists(SHADER_SOURCE)) { try { auto t = fs::last_write_time(SHADER_SOURCE); if (t > lastShaderWriteTime) { vkDeviceWaitIdle(device); reloadShader(); lastShaderWriteTime = t; } } catch(...) {} }
            vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
            updateSSBO(); 
            uint32_t imageIndex; VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
            if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); continue; } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) throw std::runtime_error("failed acquire");
            vkResetFences(device, 1, &inFlightFences[currentFrame]); 
            recordCommandBuffer(commandBuffers[imageIndex], imageIndex);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; 
            VkSemaphore ws[]={imageAvailable[currentFrame]}; 
            VkPipelineStageFlags wst[]={VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT}; 
            si.waitSemaphoreCount=1; si.pWaitSemaphores=ws; si.pWaitDstStageMask=wst; 
            si.commandBufferCount=1; si.pCommandBuffers=&commandBuffers[imageIndex]; 
            VkSemaphore ss[]={renderFinished[currentFrame]}; 
            si.signalSemaphoreCount=1; si.pSignalSemaphores=ss;
            if (vkQueueSubmit(computeQueue, 1, &si, inFlightFences[currentFrame]) != VK_SUCCESS) throw std::runtime_error("failed submit");
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; 
            pi.waitSemaphoreCount=1; pi.pWaitSemaphores=ss; 
            pi.swapchainCount=1; pi.pSwapchains=&swapchain; pi.pImageIndices=&imageIndex;
            res = vkQueuePresentKHR(presentQueue, &pi);
            if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) { framebufferResized = false; recreateSwapchain(); } else if (res != VK_SUCCESS) throw std::runtime_error("failed present");
            currentFrame = (currentFrame + 1) % 2;
        }
        vkDeviceWaitIdle(device);
    }

    void cleanup() {
        vkDeviceWaitIdle(device); 
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
        vkDestroyRenderPass(device, imguiRenderPass, nullptr);
        cleanupSwapchain(); 
        vkDestroyPipeline(device, computePipeline, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkUnmapMemory(device, sceneBufferMemory); vkDestroyBuffer(device, sceneBuffer, nullptr); vkFreeMemory(device, sceneBufferMemory, nullptr);
        for(int i=0; i<2; i++) { vkDestroySemaphore(device, imageAvailable[i], nullptr); vkDestroySemaphore(device, renderFinished[i], nullptr); vkDestroyFence(device, inFlightFences[i], nullptr); }
        vkDestroyCommandPool(device, commandPool, nullptr); vkDestroyDevice(device, nullptr); vkDestroySurfaceKHR(instance, surface, nullptr); vkDestroyInstance(instance, nullptr); glfwDestroyWindow(window); glfwTerminate();
    }
};

int main() { 
    try { Engine e; e.run(); } 
    catch (const std::exception& x) { std::cerr << "\n[CRITICAL ERROR]: " << x.what() << std::endl; std::cin.get(); return EXIT_FAILURE; } 
    return EXIT_SUCCESS; 
}