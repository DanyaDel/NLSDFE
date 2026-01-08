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