#pragma once
#include "Common.h"
#include <vector>

struct UndoState {
    std::vector<ObjectData> objects;
    std::vector<int> selection;
};

extern std::vector<UndoState> undoStack;

uint32_t calculateChecksum(const std::vector<ObjectData>& objects);
void saveUndo();

// Эти функции реализуются в Scene.cpp
void performUndo();
void deleteSelectedObject();
void duplicateSelectedObject();
void resetScene(); // Бывшая initDefaultScene
void updateObjectMatrices();

vec3 getSelectionCenter();
std::vector<char> readFile(const std::string& filename);

struct SceneNode {
    std::string name;
    
    // Локальные трансформации (относительно родителя)
    vec3 localPosition = {0,0,0};
    vec3 localRotation = {0,0,0};
    vec3 localScale    = {1,1,1};

    // Данные для GPU (будут пересчитываться в мировые координаты)
    ObjectData gpuData; 

    int parent = -1; // Индекс родителя (-1 если корень)
    std::vector<int> children;

    bool isOpen = true; // Для UI (развернут ли список в иерархии)
};

extern std::vector<SceneNode> sceneGraph;

