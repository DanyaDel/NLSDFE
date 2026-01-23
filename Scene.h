#pragma once
#include "Common.h"
#include <vector>

struct UndoState {
    std::vector<SceneNode> objects;
    std::vector<int> selection;
};

extern std::vector<UndoState> undoStack;

uint32_t calculateChecksum(const std::vector<ObjectData>& objects);
uint32_t calculateChecksum(const std::vector<SceneNode>& nodes);
void saveUndo();

// Эти функции реализуются в Scene.cpp
void performUndo();
void deleteSelectedObject();
void duplicateSelectedObject();
void resetScene(); 

// Hierarchy helpers
void reparent(int childIdx, int newParentIdx);
void updateHierarchy(int nodeIdx, mat4 parentWorldMat);

// --- ВАЖНО: Добавлено объявление этой функции ---
mat4 calculateWorldMatrix(int nodeIdx);
// ------------------------------------------------

vec3 getSelectionCenter();
std::vector<char> readFile(const std::string& filename);

struct OBJModel {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
OBJModel parseOBJ(const std::string& path);