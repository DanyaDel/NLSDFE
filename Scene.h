#pragma once
#include "Common.h"
#include <vector>

struct UndoState {
    std::vector<SceneNode> objects;
    std::vector<int> selection;
};

extern std::vector<UndoState> undoStack;
extern std::vector<UndoState> redoStack;

uint32_t calculateChecksum(const std::vector<SceneNode>& nodes);
void saveUndo();

// Эти функции реализуются в Scene.cpp
void performUndo();
void performRedo();
void deleteSelectedObject();
void duplicateSelectedObject();
void resetScene();
int createNode(int type, const std::string& name);   // -1, если сцена заполнена

// Hierarchy helpers
void reparent(int childIdx, int newParentIdx);
void updateHierarchy(int nodeIdx, mat4 parentWorldMat, bool parentActive = true);
mat4 calculateWorldMatrix(int nodeIdx);
bool isAncestorSelected(int nodeIdx);

// Пересчитывает мировые трансформации и собирает буферы для шейдера/коллизий.
// Вызывается раз в кадр до ввода, физики и рендера.
void updateSceneCaches();

vec3 getSelectionCenter();
std::vector<char> readFile(const std::string& filename);

// --- SDF на CPU (зеркало shaders/shader.comp: коллизии и выбор объектов) ---
const char* shapeName(int type);
float shapeParam2(int type, float raw);                 // Доп. параметр формы с дефолтом
vec3 shapeHalfExtents(int type, float size, float p2);  // Локальные полуразмеры до масштаба
float sdfObject(const GpuObject& o, vec3 p);
vec2 sceneMap(vec3 p);                                  // x = расстояние, y = индекс в gpuObjectCache
float sceneDistance(vec3 p);
vec3 sceneNormal(vec3 p);
int pickNode(Ray ray);                                  // Индекс узла под лучом или -1

// --- Материалы ---
struct MaterialPreset { const char* name; vec3 color; float metallic; float roughness; };
extern const std::vector<MaterialPreset> materialPresets;
void applyMaterialPreset(SceneNode& node, const MaterialPreset& preset);

// --- Файлы сцен (.nos) ---
inline constexpr int NOS_THUMB_W = 256;
inline constexpr int NOS_THUMB_H = 144;
bool saveScene(const std::string& path, const std::vector<uint8_t>& thumbnailRgba);
bool loadScene(const std::string& path, bool append);
bool readNosThumbnail(const std::string& path, std::vector<uint8_t>& rgba);

struct OBJModel {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
OBJModel parseOBJ(const std::string& path);
