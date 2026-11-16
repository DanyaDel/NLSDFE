#pragma once
#include "Common.h"
#include <vector>
#include <functional>

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

// --- Узлы во время игры ---
// Удаление откладывается: destroyNode прячет узел с потомками сразу, а flushDestroyed
// (раз в кадр, когда не работают ни скрипты, ни физика) освобождает их места.
void destroyNode(int nodeIdx);
void flushDestroyed();
// Копия узла с потомками в виде самостоятельного набора (родители — индексы внутри набора)
std::vector<SceneNode> copySubtree(int nodeIdx);
// Вставляет набор узлов в сцену, занимая освобождённые места. Возвращает индекс корня
// (если корней несколько — общий пустой узел) или -1, когда места не хватает.
int spawnNodes(std::vector<SceneNode> nodes, int parent = -1);
// Читает узлы из .nos, не трогая сцену
bool readSceneNodes(const std::string& path, std::vector<SceneNode>& nodes);

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
float sampleVolume(int offset, int resolution, vec3 local);   // local — в кубе [-1, 1]^3 (снаружи — до куба плюс значение на границе)
vec2 mapObjectList(const std::vector<GpuObject>& objects, vec3 p);   // CSG по списку; первый объект — всегда основа
vec2 sceneMap(vec3 p);                                  // x = расстояние, y = индекс в gpuObjectCache
float sceneDistance(vec3 p);
vec3 sceneNormal(vec3 p);
vec3 nodeHalfExtents(int nodeIdx, vec3* localCenter = nullptr);   // Габариты узла в его осях с учётом масштаба
int pickNode(Ray ray);                                  // Индекс узла под лучом или -1

// --- Материалы ---
struct MaterialPreset { const char* name; vec3 color; float metallic; float roughness; };
extern const std::vector<MaterialPreset> materialPresets;
void applyMaterialPreset(SceneNode& node, const MaterialPreset& preset);

// --- Файлы сцен (.nos) ---
// Загрузчик мешей подставляет приложение (нужен GPU): путь -> индекс в meshCache или -1
extern std::function<int(const std::string&)> g_meshLoader;
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
