#include "Scene.h"
#include "MathUtils.h"
#include <fstream>
#include <algorithm>
#include <numeric>

#include <sstream>

OBJModel parseOBJ(const std::string& path) {
    OBJModel model;
    std::ifstream file(path);
    if (!file.is_open()) return model;

    std::vector<vec3> positions;
    std::vector<vec3> normals;
    std::vector<vec2> uvs;
    
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            vec3 v; ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        } else if (prefix == "vn") {
            vec3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (prefix == "vt") {
            vec2 t; ss >> t.x >> t.y;
            uvs.push_back(t);
        } else if (prefix == "f") {
            std::string vertexData;
            for (int i = 0; i < 3; ++i) {
                ss >> vertexData;
                std::replace(vertexData.begin(), vertexData.end(), '/', ' ');
                std::stringstream vss(vertexData);
                int vIdx = -1, tIdx = -1, nIdx = -1;
                vss >> vIdx;
                if (vertexData.find(' ') != std::string::npos) {
                    if (line.find("//") != std::string::npos) vss >> nIdx;
                    else {
                        vss >> tIdx;
                        if (vss >> nIdx);
                    }
                }

                Vertex v = {};
                if (vIdx > 0 && vIdx <= (int)positions.size()) v.pos = positions[vIdx - 1];
                if (nIdx > 0 && nIdx <= (int)normals.size()) v.normal = normals[nIdx - 1];
                if (tIdx > 0 && tIdx <= (int)uvs.size()) v.uv = uvs[tIdx - 1];
                v.color = {1, 1, 1, 1};

                model.vertices.push_back(v);
                model.indices.push_back((uint32_t)model.indices.size());
            }
        }
    }
    return model;
}

mat4 calculateWorldMatrix(int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= (int)sceneGraph.size()) return identityMatrix();
    
    mat4 parentMat = identityMatrix();
    if (sceneGraph[nodeIdx].parent != -1) {
        parentMat = calculateWorldMatrix(sceneGraph[nodeIdx].parent);
    }

    mat4 localMat = composeMatrix(
        sceneGraph[nodeIdx].localPosition, 
        sceneGraph[nodeIdx].localRotation, 
        sceneGraph[nodeIdx].localScale
    );

    return multiplyMatrices(parentMat, localMat);
}

std::vector<UndoState> undoStack;

uint32_t calculateChecksum(const std::vector<SceneNode>& nodes) {
    if (nodes.empty()) return 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&nodes[0].gpuData);
    size_t size = nodes.size() * sizeof(ObjectData);
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += data[i];
    return sum;
}

void saveUndo() {
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin()); 
    undoStack.push_back({sceneGraph, selectedIndices});
}

void performUndo() {
    if (undoStack.empty()) return;
    UndoState state = undoStack.back();
    undoStack.pop_back();
    sceneGraph = state.objects;
    selectedIndices = state.selection;
}

vec3 getSelectionCenter() {
    if (selectedIndices.empty()) return {0,0,0};
    vec3 center = {0,0,0};
    int count = 0;
    for (int idx : selectedIndices) {
        if(idx < sceneGraph.size()) {
            center.x += sceneGraph[idx].gpuData.position.x;
            center.y += sceneGraph[idx].gpuData.position.y;
            center.z += sceneGraph[idx].gpuData.position.z;
            count++;
        }
    }
    if(count > 0) center = center / (float)count;
    return center;
}

void reparent(int childIdx, int newParentIdx) {
    if (childIdx == newParentIdx) return;
    if (childIdx < 0 || childIdx >= (int)sceneGraph.size()) return;

    // Проверка циклов (нельзя сделать родителя своим ребенком)
    int checkIdx = newParentIdx;
    while (checkIdx != -1) {
        if (checkIdx == childIdx) return; 
        checkIdx = sceneGraph[checkIdx].parent;
    }

    SceneNode& child = sceneGraph[childIdx];
    
    // 1. Сохраняем текущее мировое состояние
    mat4 worldMat = calculateWorldMatrix(childIdx);
    
    // 2. Отцепляем от старого родителя
    if (child.parent != -1) {
        auto& oldChildren = sceneGraph[child.parent].children;
        oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), childIdx), oldChildren.end());
    }

    // 3. Прицепляем к новому
    child.parent = newParentIdx;
    if (newParentIdx != -1) {
        sceneGraph[newParentIdx].children.push_back(childIdx);
        
        // 4. Пересчитываем Local = InvParentWorld * World
        // Чтобы объект визуально остался на месте
        mat4 parentWorld = calculateWorldMatrix(newParentIdx);
        mat4 invParent = inverseMatrix(parentWorld);
        mat4 newLocal = multiplyMatrices(invParent, worldMat);
        
        decomposeMatrix(newLocal, child.localPosition, child.localRotation, child.localScale);
    } else {
        // Если кинули в корень, то Local = World
        decomposeMatrix(worldMat, child.localPosition, child.localRotation, child.localScale);
    }
}

void updateHierarchy(int nodeIdx, mat4 parentWorldMat) {
    if (nodeIdx >= sceneGraph.size()) return;
    SceneNode& node = sceneGraph[nodeIdx];

    mat4 localMat = composeMatrix(node.localPosition, node.localRotation, node.localScale);
    mat4 worldMat = multiplyMatrices(parentWorldMat, localMat);

    vec3 wPos, wRot, wScale;
    decomposeMatrix(worldMat, wPos, wRot, wScale);

    node.gpuData.position.x = wPos.x;
    node.gpuData.position.y = wPos.y;
    node.gpuData.position.z = wPos.z;
    
    node.gpuData.scale.x = wScale.x;
    node.gpuData.scale.y = wScale.y;
    node.gpuData.scale.z = wScale.z;

    node.gpuData.invRotation = createInverseRotationMatrix4x4(wRot);
    
    node.gpuData.rotationEuler.x = wRot.x;
    node.gpuData.rotationEuler.y = wRot.y;
    node.gpuData.rotationEuler.z = wRot.z;

    for (int childIdx : node.children) {
        updateHierarchy(childIdx, worldMat);
    }
}

void deleteSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::sort(selectedIndices.rbegin(), selectedIndices.rend());
    for (int idx : selectedIndices) {
        if (idx < sceneGraph.size()) {
            int pIdx = sceneGraph[idx].parent;
            if (pIdx != -1 && pIdx < sceneGraph.size()) {
                auto& c = sceneGraph[pIdx].children;
                c.erase(std::remove(c.begin(), c.end(), idx), c.end());
            }
            // Детей пока делаем сиротами (в корень)
            for(int child : sceneGraph[idx].children) {
                if(child < sceneGraph.size()) sceneGraph[child].parent = -1;
            }
            sceneGraph.erase(sceneGraph.begin() + idx);
            // Сдвиг индексов 
            for(auto& node : sceneGraph) {
                if(node.parent > idx) node.parent--;
                for(int& c : node.children) if(c > idx) c--;
            }
        }
    }
    selectedIndices.clear();
}

void duplicateSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::vector<int> newIndices;
    int currentSize = (int)sceneGraph.size();
    for (int idx : selectedIndices) {
        if (sceneGraph.size() >= MAX_OBJECTS) break;
        SceneNode newNode = sceneGraph[idx];
        newNode.name += "_Copy";
        newNode.localPosition.x += 1.0f;
        newNode.parent = -1; 
        newNode.children.clear(); 
        sceneGraph.push_back(newNode);
        newIndices.push_back(currentSize++);
    }
    selectedIndices = newIndices;
}

void resetScene() {
    undoStack.clear();
    selectedIndices.clear();
    sceneGraph.clear();
    
    mat4 identity = identityMatrix();
    auto addNode = [&](std::string name, int type, vec3 pos, vec3 scale, vec3 col) {
        SceneNode n;
        n.name = name;
        n.localPosition = pos;
        n.localScale = scale;
        n.gpuData.invRotation = identity;
        n.gpuData.position = {0,0,0, (float)type}; 
        n.gpuData.scale = {1,1,1,0};
        n.gpuData.params = {1.0f, 0, 0.1f, 0}; 
        n.gpuData.color = {col.x, col.y, col.z, 1.0f};
        n.gpuData.rotationEuler = {0,0,0,0};
        sceneGraph.push_back(n);
    };

    addNode("Sphere", 0, {0, 0.5f, 0}, {1,1,1}, {0.8f, 0.0f, 0.2f});
    addNode("Cube", 1, {2.0f, 0.5f, 0}, {1,1,1}, {0.1f, 0.4f, 0.8f});
    addNode("Floor", 1, {0, -1.0f, 0}, {8, 0.1f, 8}, {0.2f, 0.2f, 0.2f});

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