#include "Scene.h"
#include "MathUtils.h"
#include <fstream>
#include <algorithm>
#include <numeric>

std::vector<UndoState> undoStack;

uint32_t calculateChecksum(const std::vector<ObjectData>& objects) {
    if (objects.empty()) return 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(objects.data());
    size_t size = objects.size() * sizeof(ObjectData);
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += data[i];
    return sum;
}

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
}

void duplicateSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::vector<int> newIndices;
    int currentSize = (int)sceneObjects.size();
    for (int idx : selectedIndices) {
        if (sceneObjects.size() >= MAX_OBJECTS) break;
        if (idx >= sceneObjects.size()) continue;
        ObjectData newObj = sceneObjects[idx];
        newObj.position.x += 0.5f; 
        newObj.position.z += 0.5f;
        sceneObjects.push_back(newObj);
        newIndices.push_back(currentSize++);
    }
    selectedIndices = newIndices;
}

void updateObjectMatrices() {
    for(auto& obj : sceneObjects) {
        vec3 rot = {obj.rotationEuler.x, obj.rotationEuler.y, obj.rotationEuler.z};
        obj.invRotation = createInverseRotationMatrix4x4(rot);
    }
}

// Реализация сброса сцены (соответствует объявлению в Common.h)
void resetScene() {
    undoStack.clear();
    selectedIndices.clear();
    sceneObjects.clear();
    
    mat4 identity; 
    memset(&identity, 0, sizeof(mat4)); 
    identity.m[0]=1; identity.m[5]=1; identity.m[10]=1; identity.m[15]=1;
    
    // Дефолтные объекты
    sceneObjects.push_back({identity, {0, 0.5f, 0, 0.0f}, {1,1,1,0}, {0.8f, 0.0f, 0.2f, 1.0f}, {1.0f, 0.8f, 0.1f, 0.2f}, {0,0,0,0}});
    sceneObjects.push_back({identity, {0.6f, 0.8f, 0.4f, 1.0f}, {0.8f, 0.8f, 0.8f, 0}, {0.6f, 1.0f, 0.1f, 0.0f}, {0.2f, 0.4f, 1.0f, 0.8f}, {0.5f, 0.5f, 0.0f, 0}});
    sceneObjects.push_back({identity, {0, -1.0f, 0, 1.0f}, {6, 0.1f, 6, 0}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.1f, 0.1f, 0.15f, 0.9f}, {0,0,0,0}});
    
    currentLoadedFile = "Untitled";
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename);
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();
    return buf;
}