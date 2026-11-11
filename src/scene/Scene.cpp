#include "Scene.h"
#include "MathUtils.h"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <cstring>
#include <filesystem>

// =================================================================
// --- OBJ ---
// =================================================================
OBJModel parseOBJ(const std::string& path) {
    OBJModel model;
    std::ifstream file(path);
    if (!file.is_open()) return model;

    std::vector<vec3> positions;
    std::vector<vec3> normals;
    std::vector<vec2> uvs;
    std::vector<vec3> colors;
    bool missingNormals = false;

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            vec3 v; ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
            // Необязательный цвет вершины: "v x y z r g b" (так пишут Blender и сам движок)
            vec3 col(1.0f);
            if (ss >> col.r >> col.g >> col.b) {
                colors.resize(positions.size(), vec3(1.0f));
                colors.back() = col;
            }
        } else if (prefix == "vn") {
            vec3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (prefix == "vt") {
            vec2 t; ss >> t.x >> t.y;
            uvs.push_back(t);
        } else if (prefix == "f") {
            // Формат вершины: v, v/t, v//n, v/t/n; индексы могут быть отрицательными
            std::vector<Vertex> face;
            std::string vertexData;
            while (ss >> vertexData) {
                int idx[3] = {0, 0, 0};
                size_t start = 0;
                for (int k = 0; k < 3 && start <= vertexData.size(); k++) {
                    size_t slash = vertexData.find('/', start);
                    std::string part = vertexData.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                    if (!part.empty()) { try { idx[k] = std::stoi(part); } catch (...) {} }
                    if (slash == std::string::npos) break;
                    start = slash + 1;
                }
                auto resolve = [](int i, size_t count) { return i > 0 ? i - 1 : (i < 0 ? (int)count + i : -1); };
                int vIdx = resolve(idx[0], positions.size());
                int tIdx = resolve(idx[1], uvs.size());
                int nIdx = resolve(idx[2], normals.size());

                Vertex v = {};
                if (vIdx >= 0 && vIdx < (int)positions.size()) v.pos = positions[vIdx];
                if (nIdx >= 0 && nIdx < (int)normals.size()) v.normal = normals[nIdx]; else missingNormals = true;
                if (tIdx >= 0 && tIdx < (int)uvs.size()) v.uv = uvs[tIdx];
                v.color = (vIdx >= 0 && vIdx < (int)colors.size()) ? vec4(colors[vIdx], 1.0f) : vec4(1, 1, 1, 1);
                face.push_back(v);
            }
            // Триангуляция веером: квадраты и n-угольники тоже грузятся
            for (size_t i = 1; i + 1 < face.size(); i++) {
                Vertex tri[3] = { face[0], face[i], face[i + 1] };
                if (missingNormals) {
                    vec3 n = glm::cross(tri[1].pos - tri[0].pos, tri[2].pos - tri[0].pos);
                    float len = glm::length(n);
                    n = len > 1e-12f ? n / len : vec3(0, 1, 0);
                    for (auto& v : tri) if (glm::dot(v.normal, v.normal) < 1e-12f) v.normal = n;
                }
                for (auto& v : tri) {
                    model.vertices.push_back(v);
                    model.indices.push_back((uint32_t)model.indices.size());
                }
            }
        }
    }
    return model;
}

// =================================================================
// --- Undo / Redo ---
// =================================================================
std::vector<UndoState> undoStack;
std::vector<UndoState> redoStack;
static const size_t UNDO_LIMIT = 100;

uint32_t calculateChecksum(const std::vector<SceneNode>& nodes) {
    uint32_t sum = 0;
    for (const auto& node : nodes) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&node.gpuData);
        for (size_t i = 0; i < sizeof(ObjectData); ++i) sum += data[i];
    }
    return sum;
}

void saveUndo() {
    if (undoStack.size() >= UNDO_LIMIT) undoStack.erase(undoStack.begin());
    undoStack.push_back({sceneGraph, selectedIndices});
    redoStack.clear();
}

void performUndo() {
    if (undoStack.empty()) return;
    redoStack.push_back({sceneGraph, selectedIndices});
    UndoState state = std::move(undoStack.back());
    undoStack.pop_back();
    sceneGraph = std::move(state.objects);
    selectedIndices = std::move(state.selection);
}

void performRedo() {
    if (redoStack.empty()) return;
    undoStack.push_back({sceneGraph, selectedIndices});
    UndoState state = std::move(redoStack.back());
    redoStack.pop_back();
    sceneGraph = std::move(state.objects);
    selectedIndices = std::move(state.selection);
}

// =================================================================
// --- Hierarchy ---
// =================================================================
void updateHierarchy(int nodeIdx, mat4 parentWorldMat, bool parentActive) {
    if (nodeIdx < 0 || nodeIdx >= (int)sceneGraph.size()) return;
    SceneNode& node = sceneGraph[nodeIdx];

    mat4 localMat = composeMatrix(node.localPosition, node.localRotation, node.localScale);
    mat4 worldMat = parentWorldMat * localMat;

    vec3 wScale;
    mat3 wRotMat = extractRotation(worldMat, wScale);
    float ry, rx, rz;
    glm::extractEulerAngleYXZ(mat4(wRotMat), ry, rx, rz);

    node.gpuData.position = vec4(vec3(worldMat[3]), node.gpuData.position.w);
    node.gpuData.scale = vec4(wScale, 0);
    node.gpuData.invRotation = mat4(glm::transpose(wRotMat));
    node.gpuData.rotationEuler = vec4(rx, ry, rz, node.gpuData.rotationEuler.w);
    node.worldActive = parentActive && node.active;

    for (int childIdx : node.children) {
        updateHierarchy(childIdx, worldMat, node.worldActive);
    }
}

mat4 calculateWorldMatrix(int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= (int)sceneGraph.size()) return mat4(1.0f);
    mat4 local = composeMatrix(sceneGraph[nodeIdx].localPosition, sceneGraph[nodeIdx].localRotation, sceneGraph[nodeIdx].localScale);
    if (sceneGraph[nodeIdx].parent == -1) return local;
    return calculateWorldMatrix(sceneGraph[nodeIdx].parent) * local;
}

bool isAncestorSelected(int nodeIdx) {
    int p = sceneGraph[nodeIdx].parent;
    while (p != -1) {
        if (std::find(selectedIndices.begin(), selectedIndices.end(), p) != selectedIndices.end()) return true;
        p = sceneGraph[p].parent;
    }
    return false;
}

vec3 getSelectionCenter() {
    if (selectedIndices.empty()) return vec3(0, 0, 0);
    vec3 center(0, 0, 0);
    int count = 0;
    for (int idx : selectedIndices) {
        if (idx >= 0 && idx < (int)sceneGraph.size()) {
            center += vec3(sceneGraph[idx].gpuData.position);
            count++;
        }
    }
    if (count > 0) center /= (float)count;
    return center;
}

void reparent(int childIdx, int newParentIdx) {
    if (childIdx == newParentIdx) return;
    if (childIdx < 0 || childIdx >= (int)sceneGraph.size()) return;

    int checkIdx = newParentIdx;
    while (checkIdx != -1) {
        if (checkIdx == childIdx) return;
        checkIdx = sceneGraph[checkIdx].parent;
    }

    SceneNode& child = sceneGraph[childIdx];
    mat4 worldMat = calculateWorldMatrix(childIdx);

    if (child.parent != -1) {
        auto& oldChildren = sceneGraph[child.parent].children;
        oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), childIdx), oldChildren.end());
    }

    child.parent = newParentIdx;
    if (newParentIdx != -1) {
        sceneGraph[newParentIdx].children.push_back(childIdx);
        mat4 parentWorld = calculateWorldMatrix(newParentIdx);
        mat4 invParent = glm::inverse(parentWorld);
        mat4 newLocal = invParent * worldMat;
        decomposeMatrix(newLocal, child.localPosition, child.localRotation, child.localScale);
    } else {
        decomposeMatrix(worldMat, child.localPosition, child.localRotation, child.localScale);
    }
}

void deleteSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::sort(selectedIndices.rbegin(), selectedIndices.rend());
    selectedIndices.erase(std::unique(selectedIndices.begin(), selectedIndices.end()), selectedIndices.end());
    for (int idx : selectedIndices) {
        if (idx < 0 || idx >= (int)sceneGraph.size()) continue;
        // Дети становятся корневыми и остаются на своих местах
        std::vector<int> orphans = sceneGraph[idx].children;
        for (int child : orphans) reparent(child, -1);

        int pIdx = sceneGraph[idx].parent;
        if (pIdx != -1 && pIdx < (int)sceneGraph.size()) {
            auto& c = sceneGraph[pIdx].children;
            c.erase(std::remove(c.begin(), c.end(), idx), c.end());
        }
        sceneGraph.erase(sceneGraph.begin() + idx);
        for (auto& node : sceneGraph) {
            if (node.parent > idx) node.parent--;
            for (int& c : node.children) if (c > idx) c--;
            if (node.triggerTarget == idx) node.triggerTarget = -1;
            else if (node.triggerTarget > idx) node.triggerTarget--;
            for (auto& v : node.script.vars) {
                if (v.node == idx) v.node = -1;
                else if (v.node > idx) v.node--;
            }
        }
    }
    selectedIndices.clear();
}

void duplicateSelectedObject() {
    if (selectedIndices.empty()) return;
    saveUndo();
    std::vector<int> newIndices;
    for (int idx : selectedIndices) {
        if (idx < 0 || idx >= (int)sceneGraph.size()) continue;
        if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); break; }
        SceneNode newNode = sceneGraph[idx];
        newNode.name += "_Copy";
        newNode.localPosition.x += 1.0f;
        newNode.children.clear();
        int newIdx = (int)sceneGraph.size();
        sceneGraph.push_back(newNode);
        if (newNode.parent != -1) sceneGraph[newNode.parent].children.push_back(newIdx);
        newIndices.push_back(newIdx);
    }
    selectedIndices = newIndices;
}

int createNode(int type, const std::string& name) {
    if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); return -1; }
    saveUndo();
    SceneNode n;
    n.name = name;
    n.gpuData.position.w = (float)type;
    n.gpuData.params = vec4(1.0f, 0, 0, 0);
    n.gpuData.color = vec4(1, 1, 1, 0.5f);
    n.localPosition = cam.target;
    if (type == NODE_CAMERA) {
        // Новая камера встаёт на место вида редактора и смотрит туда же
        CameraBasis b = getCameraBasis();
        n.localPosition = b.pos;
        n.localRotation = { std::asin(std::clamp(b.fwd.y, -1.0f, 1.0f)), std::atan2(-b.fwd.x, -b.fwd.z), 0.0f };
    }
    if (type == NODE_LIGHT) {
        n.localPosition.y += 2.0f;
        n.gpuData.color = vec4(1.0f, 0.9f, 0.7f, 0.5f);
    }
    sceneGraph.push_back(n);
    selectedIndices = { (int)sceneGraph.size() - 1 };
    return selectedIndices[0];
}

void resetScene() {
    undoStack.clear();
    redoStack.clear();
    selectedIndices.clear();
    sceneGraph.clear();
    world = WorldSettings();

    spdlog::info("Resetting scene to default");

    auto addNode = [&](std::string name, int type, vec3 pos, vec3 scale, vec3 col) {
        SceneNode n;
        n.name = name;
        n.localPosition = pos;
        n.localScale = scale;
        n.gpuData.position = vec4(0, 0, 0, (float)type);
        n.gpuData.params = vec4(1.0f, 0, 0.1f, 0);
        n.gpuData.color = vec4(col, 1.0f);
        sceneGraph.push_back(n);
    };

    addNode("Sphere", SDF_SPHERE, {0, 0.5f, 0}, {1,1,1}, {0.8f, 0.0f, 0.2f});
    addNode("Cube", SDF_BOX, {2.0f, 0.5f, 0}, {1,1,1}, {0.1f, 0.4f, 0.8f});
    addNode("Floor", SDF_BOX, {0, -1.0f, 0}, {8, 0.1f, 8}, {0.2f, 0.2f, 0.2f});

    // Камера, через которую смотрит Play Mode
    SceneNode camera;
    camera.name = "Camera";
    camera.gpuData.position.w = (float)NODE_CAMERA;
    camera.localPosition = {0.0f, 2.2f, 7.0f};
    camera.localRotation = {-0.2f, 0.0f, 0.0f};
    sceneGraph.push_back(camera);

    currentLoadedFile = "Untitled";
}

// =================================================================
// --- SDF на CPU (должно совпадать с shaders/shader.comp) ---
// =================================================================
const char* shapeName(int type) {
    switch (type) {
        case SDF_SPHERE: return "Sphere";
        case SDF_BOX: return "Box";
        case SDF_CYLINDER: return "Cylinder";
        case SDF_TORUS: return "Torus";
        case SDF_CAPSULE: return "Capsule";
        case SDF_CONE: return "Cone";
        case SDF_VOLUME: return "Volume";
        case NODE_LIGHT: return "Light";
        case NODE_CAMERA: return "Camera";
        default: return "Empty";
    }
}

float shapeParam2(int type, float raw) {
    if (type == SDF_TORUS) return raw > 0.0f ? raw : 0.3f;     // Толщина относительно радиуса
    if (type == SDF_CAPSULE) return raw > 0.0f ? raw : 0.4f;   // Радиус относительно полувысоты
    if (type == SDF_CONE) return std::clamp(raw, 0.0f, 1.0f);  // Верхний радиус относительно нижнего
    return raw;
}

vec3 shapeHalfExtents(int type, float size, float p2) {
    size = std::abs(size);
    switch (type) {
        case SDF_TORUS: return { size * (1.0f + p2), size * p2, size * (1.0f + p2) };
        case SDF_CAPSULE: return { size * p2, size * (1.0f + p2), size * p2 };
        default: return vec3(size);
    }
}

static float sdBox(vec3 p, vec3 b) {
    vec3 q = glm::abs(p) - b;
    return glm::length(glm::max(q, vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
}
static float sdCylinder(vec3 p, float h, float r) {
    vec2 d = glm::abs(vec2(glm::length(vec2(p.x, p.z)), p.y)) - vec2(r, h);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, vec2(0.0f)));
}
static float sdCone(vec3 p, float h, float r1, float r2) {
    vec2 q = vec2(glm::length(vec2(p.x, p.z)), p.y);
    vec2 k1 = vec2(r2, h);
    vec2 k2 = vec2(r2 - r1, 2.0f * h);
    vec2 ca = vec2(q.x - std::min(q.x, (q.y < 0.0f) ? r1 : r2), std::abs(q.y) - h);
    vec2 cb = q - k1 + k2 * std::clamp(glm::dot(k1 - q, k2) / glm::dot(k2, k2), 0.0f, 1.0f);
    float s = (cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f;
    return s * std::sqrt(std::min(glm::dot(ca, ca), glm::dot(cb, cb)));
}

float sampleVolume(int offset, int res, vec3 v) {
    if (res < 2 || offset < 0 || offset + res * res * res > (int)volumeData.size()) return glm::length(v) - 1.0f;
    vec3 c = glm::clamp(v, vec3(-1.0f), vec3(1.0f));
    float outside = glm::length(v - c);
    vec3 g = (c * 0.5f + 0.5f) * (float)(res - 1);
    glm::ivec3 i0 = glm::min(glm::ivec3(glm::floor(g)), glm::ivec3(res - 2));
    vec3 f = g - vec3(i0);
    const float* d = volumeData.data() + offset;
    auto at = [&](int x, int y, int z) { return d[(i0.x + x) + res * ((i0.y + y) + res * (i0.z + z))]; };
    float x00 = glm::mix(at(0, 0, 0), at(1, 0, 0), f.x), x10 = glm::mix(at(0, 1, 0), at(1, 1, 0), f.x);
    float x01 = glm::mix(at(0, 0, 1), at(1, 0, 1), f.x), x11 = glm::mix(at(0, 1, 1), at(1, 1, 1), f.x);
    return outside + glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
}

float sdfObject(const GpuObject& o, vec3 p) {
    vec3 q = mat3(vec3(o.rot0), vec3(o.rot1), vec3(o.rot2)) * (p - vec3(o.pos));
    vec3 scl = vec3(o.scale);
    float size = o.pos.w;
    int type = (int)o.rot0.w;
    if (type == SDF_BOX) return sdBox(q, size * scl);

    float minScale = std::min(scl.x, std::min(scl.y, scl.z));
    vec3 u = q / scl;
    float p2 = o.scale.w;
    float d;
    if (type == SDF_CYLINDER) d = sdCylinder(u, size, size);
    else if (type == SDF_TORUS) d = glm::length(vec2(glm::length(vec2(u.x, u.z)) - size, u.y)) - size * p2;
    else if (type == SDF_CAPSULE) { u.y -= std::clamp(u.y, -size, size); d = glm::length(u) - size * p2; }
    else if (type == SDF_CONE) d = sdCone(u, size, size, size * p2);
    else if (type == SDF_VOLUME) d = sampleVolume((int)o.extra.w, (int)o.scale.w, u / size) * size;
    else d = glm::length(u) - size;
    return d * minScale;
}

static vec2 opSmoothUnion(vec2 d1, vec2 d2, float k) {
    float h = std::clamp(0.5f + 0.5f * (d2.x - d1.x) / k, 0.0f, 1.0f);
    return vec2(glm::mix(d2.x, d1.x, h) - k * h * (1.0f - h), (d1.x < d2.x) ? d1.y : d2.y);
}
static vec2 opSmoothSub(vec2 d1, vec2 d2, float k) {
    float h = std::clamp(0.5f - 0.5f * (d2.x + d1.x) / k, 0.0f, 1.0f);
    return vec2(glm::mix(d2.x, -d1.x, h) + k * h * (1.0f - h), d2.y);
}
static vec2 opSmoothIntersect(vec2 d1, vec2 d2, float k) {
    float h = std::clamp(0.5f - 0.5f * (d2.x - d1.x) / k, 0.0f, 1.0f);
    return vec2(glm::mix(d2.x, d1.x, h) + k * h * (1.0f - h), (d1.x > d2.x) ? d1.y : d2.y);
}

vec2 mapObjectList(const std::vector<GpuObject>& objects, vec3 p) {
    vec2 res(100.0f, -1.0f);
    for (int i = 0; i < (int)objects.size(); i++) {
        const GpuObject& o = objects[i];
        vec2 cur(sdfObject(o, p), (float)i);
        int op = (i == 0) ? OP_UNION : (int)o.rot1.w;
        float k = o.rot2.w;
        if (op == OP_UNION) res = opSmoothUnion(res, cur, k);
        else if (op == OP_SUBTRACT) res = opSmoothSub(cur, res, k);
        else res = opSmoothIntersect(res, cur, k);
    }
    return res;
}

vec2 sceneMap(vec3 p) {
    vec2 res(100.0f, -1.0f);
    for (int i = 0; i < (int)gpuObjectCache.size(); i++) {
        const GpuObject& o = gpuObjectCache[i];
        vec2 cur(sdfObject(o, p), (float)i);
        int op = (i == 0) ? OP_UNION : (int)o.rot1.w;   // i == 0 — это и есть объект с extra.z = 1
        float k = o.rot2.w;
        if (op == OP_UNION) res = opSmoothUnion(res, cur, k);
        else if (op == OP_SUBTRACT) res = opSmoothSub(cur, res, k);
        else res = opSmoothIntersect(res, cur, k);
    }
    return res;
}

float sceneDistance(vec3 p) { return sceneMap(p).x; }

vec3 sceneNormal(vec3 p) {
    const float e = 0.002f;
    vec3 n(
        sceneDistance(p + vec3(e, 0, 0)) - sceneDistance(p - vec3(e, 0, 0)),
        sceneDistance(p + vec3(0, e, 0)) - sceneDistance(p - vec3(0, e, 0)),
        sceneDistance(p + vec3(0, 0, e)) - sceneDistance(p - vec3(0, 0, e)));
    float len = glm::length(n);
    return len > 1e-8f ? n / len : vec3(0, 1, 0);
}

vec3 nodeHalfExtents(int nodeIdx, vec3* localCenter) {
    const SceneNode& node = sceneGraph[nodeIdx];
    vec3 scl = glm::max(glm::abs(vec3(node.gpuData.scale)), vec3(1e-4f));
    if (localCenter) *localCenter = vec3(0.0f);
    if (node.meshIdx >= 0 && node.meshIdx < (int)meshCache.size()) {
        const MeshData& mesh = meshCache[node.meshIdx];
        if (localCenter) *localCenter = (mesh.boundsMin + mesh.boundsMax) * 0.5f * scl;
        return glm::max((mesh.boundsMax - mesh.boundsMin) * 0.5f * scl, vec3(1e-3f));
    }
    int type = nodeType(node);
    if (type >= 0) return shapeHalfExtents(type, node.gpuData.params.x, shapeParam2(type, node.gpuData.rotationEuler.w)) * scl;
    return vec3(0.5f) * scl;
}

// Объект поля столкновений для узла с коллайдером Box / Sphere / Capsule
static GpuObject makeColliderObject(int nodeIdx) {
    const SceneNode& node = sceneGraph[nodeIdx];
    const ObjectData& d = node.gpuData;
    vec3 center;
    vec3 he = nodeHalfExtents(nodeIdx, &center) * glm::max(glm::abs(node.collider.size), vec3(1e-3f));
    mat3 rot = glm::transpose(mat3(d.invRotation));

    GpuObject o;
    int type = SDF_BOX;
    float size = 1.0f, p2 = 0.0f;
    vec3 scl = he;
    if (node.collider.shape == COLLIDER_SPHERE) {
        type = SDF_SPHERE;
        scl = vec3(std::max(he.x, std::max(he.y, he.z)));
    } else if (node.collider.shape == COLLIDER_CAPSULE) {
        float r = std::max(he.x, he.z);
        float halfSegment = he.y - r;
        if (halfSegment > 0.01f * r) { type = SDF_CAPSULE; scl = vec3(r); size = halfSegment / r; p2 = r / halfSegment; }
        else { type = SDF_SPHERE; scl = vec3(r); }
    }
    o.rot0 = vec4(vec3(d.invRotation[0]), (float)type);
    o.rot1 = vec4(vec3(d.invRotation[1]), (float)OP_UNION);
    o.rot2 = vec4(vec3(d.invRotation[2]), 0.001f);
    o.pos = vec4(vec3(d.position) + rot * center, size);
    o.scale = vec4(scl, p2);
    o.color = d.color;
    o.bound = vec4(shapeHalfExtents(type, size, p2) * scl + vec3(0.26f), 0.0f);
    o.extra = vec4(0.0f, (float)nodeIdx, 0.0f, 0.0f);
    return o;
}

// Собирает сетки активных узлов в volumeData; пересобирает, только если набор сеток изменился
static std::vector<int> layoutVolumes() {
    static std::vector<const SdfVolume*> order;
    std::vector<const SdfVolume*> current;
    std::vector<int> offsets(sceneGraph.size(), -1);
    for (const SceneNode& n : sceneGraph) {
        if (nodeType(n) != SDF_VOLUME || !n.volume || !n.worldActive) continue;
        if (std::find(current.begin(), current.end(), n.volume.get()) == current.end()) current.push_back(n.volume.get());
    }
    if (current != order) {
        order = current;
        volumeData.clear();
        for (const SdfVolume* v : order) volumeData.insert(volumeData.end(), v->distances.begin(), v->distances.end());
        volumeDataVersion++;
    }
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& n = sceneGraph[i];
        if (nodeType(n) != SDF_VOLUME || !n.volume || !n.worldActive) continue;
        int offset = 0;
        for (const SdfVolume* v : order) { if (v == n.volume.get()) break; offset += (int)v->distances.size(); }
        offsets[i] = offset;
    }
    return offsets;
}

void updateSceneCaches() {
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        if (sceneGraph[i].parent == -1) updateHierarchy(i, mat4(1.0f));
    }
    std::vector<int> volumeOffsets = layoutVolumes();

    gpuObjectCache.clear();
    gpuLightCache.clear();
    collisionCache.clear();
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& node = sceneGraph[i];
        if (!node.worldActive) continue;
        int type = nodeType(node);

        if (type == NODE_LIGHT) {
            if (gpuLightCache.size() < MAX_LIGHTS) {
                GpuLight l;
                l.posRange = vec4(vec3(node.gpuData.position), std::max(node.lightRange, 0.01f));
                l.color = vec4(vec3(node.gpuData.color) * node.lightIntensity, 0.0f);
                gpuLightCache.push_back(l);
            }
        }

        // Коллайдер, заданный явно, не зависит от того, есть ли у узла видимая форма
        bool overrideCollider = node.collider.shape == COLLIDER_BOX || node.collider.shape == COLLIDER_SPHERE || node.collider.shape == COLLIDER_CAPSULE;
        if (overrideCollider && !node.isTrigger && collisionCache.size() < MAX_OBJECTS) collisionCache.push_back(makeColliderObject(i));

        if (!isSdfNode(node) || node.isTrigger) continue;
        if (gpuObjectCache.size() >= MAX_OBJECTS) continue;
        if (type == SDF_VOLUME && volumeOffsets[i] < 0) continue;   // Сетки нет — рисовать нечего

        const ObjectData& d = node.gpuData;
        vec3 scl = glm::max(glm::abs(vec3(d.scale)), vec3(1e-4f));
        float size = d.params.x;
        float p2 = shapeParam2(type, d.rotationEuler.w);
        int op = std::clamp((int)std::lround(d.params.y), 0, (int)OP_INTERSECT);
        float k = std::max(d.params.z, 0.001f);
        bool selected = std::find(selectedIndices.begin(), selectedIndices.end(), i) != selectedIndices.end();

        GpuObject o;
        o.rot0 = vec4(vec3(d.invRotation[0]), (float)type);
        o.rot1 = vec4(vec3(d.invRotation[1]), (float)op);
        o.rot2 = vec4(vec3(d.invRotation[2]), k);
        o.pos = vec4(vec3(d.position), size);
        o.scale = vec4(scl, p2);
        o.color = d.color;
        // Пересечение режет всё вокруг себя, поэтому отсекать его по лучу нельзя
        vec3 bound = (op == OP_INTERSECT) ? vec3(1e9f) : shapeHalfExtents(type, size, p2) * scl + vec3(k + 0.25f);
        o.bound = vec4(bound, d.params.w);
        o.extra = vec4(selected ? 1.0f : 0.0f, (float)i, gpuObjectCache.empty() ? 1.0f : 0.0f, 0.0f);
        if (type == SDF_VOLUME) { o.scale.w = (float)node.volume->resolution; o.extra.w = (float)volumeOffsets[i]; }
        gpuObjectCache.push_back(o);

        // Коллайдер «по форме»: то же тело, что на экране, вместе с его CSG-операцией
        if (node.collider.shape == COLLIDER_SHAPE && collisionCache.size() < MAX_OBJECTS) {
            o.extra.x = 0.0f;
            collisionCache.push_back(o);
        }
    }
}

int pickNode(Ray ray) {
    float bestT = 1e9f;
    int best = -1;

    // 1. Точное попадание: марш по той же SDF, что видит пользователь
    float t = 0.05f;
    for (int i = 0; i < 160 && t < 200.0f; i++) {
        vec3 p = ray.origin + ray.dir * t;
        vec2 r = sceneMap(p);
        if (r.x < 0.0005f + 0.001f * t) {
            if (r.y >= 0.0f) {
                bestT = t;
                best = (int)gpuObjectCache[(int)r.y].extra.y;
                // Поверхность, вырезанная вычитанием, принадлежит вычитающему объекту
                float closest = 1e9f;
                for (int j = 1; j < (int)gpuObjectCache.size(); j++) {
                    const GpuObject& o = gpuObjectCache[j];
                    if ((int)o.rot1.w != OP_SUBTRACT) continue;
                    float d = std::abs(sdfObject(o, p));
                    if (d < 0.03f + o.rot2.w && d < closest) { closest = d; best = (int)o.extra.y; }
                }
            }
            break;
        }
        t += r.x;
    }

    // 2. Всё, что не SDF: свет, пустышки, меши, триггеры
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& node = sceneGraph[i];
        if (!node.worldActive) continue;
        if (isSdfNode(node) && !node.isTrigger) continue;
        vec3 pos = vec3(node.gpuData.position);
        float hit = -1.0f;
        if (node.meshIdx >= 0 && node.meshIdx < (int)meshCache.size()) {
            const MeshData& mesh = meshCache[node.meshIdx];
            mat4 inv = glm::inverse(calculateWorldMatrix(i));
            Ray local = { vec3(inv * vec4(ray.origin, 1.0f)), vec3(inv * vec4(ray.dir, 0.0f)) };
            hit = intersectBox(local, (mesh.boundsMin + mesh.boundsMax) * 0.5f, (mesh.boundsMax - mesh.boundsMin) * 0.5f + vec3(1e-4f));
        } else if (node.isTrigger) {
            hit = intersectBoxOBB(ray, pos, mat3(node.gpuData.invRotation), std::abs(node.gpuData.params.x) * vec3(node.gpuData.scale));
        } else {
            hit = intersectSphere(ray, pos, 0.25f);
        }
        if (hit > 0.0f && hit < bestT) { bestT = hit; best = i; }
    }

    // 3. Ничего не нашли — пробуем габариты SDF-объектов (невидимые вычитатели и т.п.)
    if (best == -1) {
        for (const GpuObject& o : gpuObjectCache) {
            int type = (int)o.rot0.w;
            vec3 he = shapeHalfExtents(type, o.pos.w, o.scale.w) * vec3(o.scale);
            float hit = intersectBoxOBB(ray, vec3(o.pos), mat3(vec3(o.rot0), vec3(o.rot1), vec3(o.rot2)), he);
            if (hit > 0.0f && hit < bestT) { bestT = hit; best = (int)o.extra.y; }
        }
    }
    return best;
}

// =================================================================
// --- Материалы ---
// =================================================================
const std::vector<MaterialPreset> materialPresets = {
    { "Gold",     {1.00f, 0.77f, 0.34f}, 1.0f, 0.25f },
    { "Chrome",   {0.90f, 0.90f, 0.92f}, 1.0f, 0.08f },
    { "Copper",   {0.95f, 0.64f, 0.54f}, 1.0f, 0.30f },
    { "Glass",    {0.85f, 0.93f, 0.95f}, 0.0f, 0.05f },   // Без преломления: гладкий диэлектрик
    { "Plastic",  {0.80f, 0.10f, 0.10f}, 0.0f, 0.35f },
    { "Rubber",   {0.08f, 0.08f, 0.08f}, 0.0f, 0.95f },
    { "Ceramic",  {0.92f, 0.90f, 0.85f}, 0.0f, 0.18f },
    { "Clay",     {0.60f, 0.45f, 0.38f}, 0.0f, 1.00f },
};

void applyMaterialPreset(SceneNode& node, const MaterialPreset& preset) {
    node.gpuData.color = vec4(preset.color, preset.roughness);
    node.gpuData.params.w = preset.metallic;
}

// =================================================================
// --- Файлы сцен (.nos) ---
// Раскладка v100 сохранена, чтобы старые сборки открывали новые файлы.
// Всё новое (имена, локальные трансформации, компоненты, мир) лежит
// в блоке расширения "NOSX" после миниатюры.
// =================================================================
std::function<int(const std::string&)> g_meshLoader;

namespace {
    const uint32_t NOS_VERSION = 200;
    const uint32_t NOSX_VERSION = 4;   // 2: твёрдые тела и скрипты; 3: коллайдер, звук, контроллер, камера; 4: воксельные сетки

    template <typename T> void writePod(std::ostream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
    template <typename T> bool readPod(std::istream& f, T& v) { f.read(reinterpret_cast<char*>(&v), sizeof(T)); return (bool)f; }
    void writeString(std::ostream& f, const std::string& s) {
        uint32_t len = (uint32_t)s.size(); writePod(f, len); f.write(s.data(), len);
    }
    bool readString(std::istream& f, std::string& s) {
        uint32_t len = 0;
        if (!readPod(f, len) || len > 4096) return false;
        s.resize(len); f.read(s.data(), len);
        return (bool)f;
    }
}

bool saveScene(const std::string& path, const std::vector<uint8_t>& thumbnailRgba) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    NosHeader header{};
    memcpy(header.magic, "NOS!", 4);
    header.version = NOS_VERSION;
    header.objectCount = (uint32_t)sceneGraph.size();
    header.checksum = calculateChecksum(sceneGraph);
    header.thumbnailOffset = (uint32_t)(sizeof(NosHeader) + sizeof(SceneMetadata) + sceneGraph.size() * (sizeof(ObjectData) + sizeof(int32_t)));
    header.thumbnailSize = (uint32_t)thumbnailRgba.size();

    SceneMetadata meta{};
    meta.target = cam.target; meta.theta = cam.theta; meta.phi = cam.phi; meta.radius = cam.radius;

    writePod(file, header);
    writePod(file, meta);
    for (const auto& node : sceneGraph) {
        writePod(file, node.gpuData);
        writePod(file, (int32_t)node.parent);
    }
    file.write(reinterpret_cast<const char*>(thumbnailRgba.data()), thumbnailRgba.size());

    file.write("NOSX", 4);
    writePod(file, NOSX_VERSION);
    writePod(file, (uint32_t)sceneGraph.size());
    for (const auto& node : sceneGraph) {
        writeString(file, node.name);
        writeString(file, node.meshPath);
        writePod(file, node.localPosition); writePod(file, node.localRotation); writePod(file, node.localScale);
        writePod(file, (uint8_t)node.active); writePod(file, (uint8_t)node.isTrigger); writePod(file, (uint8_t)node.behaviorOn);
        writePod(file, (int32_t)node.triggerTarget); writePod(file, (int32_t)node.triggerAction);
        writePod(file, (int32_t)node.behavior); writePod(file, node.behaviorVec); writePod(file, node.behaviorSpeed);
        writePod(file, node.lightIntensity); writePod(file, node.lightRange);
        // --- версия 2 ---
        const RigidBody& b = node.body;
        writePod(file, (int32_t)b.type); writePod(file, b.mass); writePod(file, b.restitution); writePod(file, b.friction);
        writePod(file, b.linearDamping); writePod(file, b.angularDamping); writePod(file, b.gravityScale);
        writeString(file, node.script.path);
        writePod(file, (uint32_t)node.script.vars.size());
        for (const auto& v : node.script.vars) {
            writeString(file, v.name); writePod(file, (int32_t)v.type); writePod(file, v.value);
            writeString(file, v.text); writePod(file, (int32_t)v.node);
        }
        // --- версия 3 ---
        writePod(file, (int32_t)node.collider.shape); writePod(file, node.collider.size);
        const AudioSource& a = node.audio;
        writePod(file, (uint8_t)a.enabled); writeString(file, a.clip); writePod(file, a.volume);
        writePod(file, (uint8_t)a.loop); writePod(file, (uint8_t)a.playOnStart); writePod(file, (uint8_t)a.spatial); writePod(file, a.range);
        const CharacterController& c = node.controller;
        writePod(file, (uint8_t)c.enabled); writePod(file, c.walkSpeed); writePod(file, c.runSpeed); writePod(file, c.jumpSpeed);
        writePod(file, c.radius); writePod(file, c.height); writePod(file, (uint8_t)c.mouseLook);
        writePod(file, node.cameraFov);
        // --- версия 4 ---
        int32_t volumeRes = node.volume ? node.volume->resolution : 0;
        writePod(file, volumeRes);
        if (volumeRes > 0) file.write(reinterpret_cast<const char*>(node.volume->distances.data()), node.volume->distances.size() * sizeof(float));
    }
    writePod(file, world.sunAzimuth); writePod(file, world.sunElevation); writePod(file, world.sunIntensity);
    writePod(file, world.sunColor); writePod(file, world.skyTop); writePod(file, world.skyHorizon); writePod(file, world.ambient);
    return (bool)file;
}

bool loadScene(const std::string& path, bool append) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    NosHeader header;
    if (!readPod(file, header) || strncmp(header.magic, "NOS!", 4) != 0) return false;
    SceneMetadata meta;
    if (!readPod(file, meta)) return false;
    if (header.objectCount > 100000) return false;

    std::vector<SceneNode> nodes;
    nodes.reserve(header.objectCount);
    for (uint32_t i = 0; i < header.objectCount; ++i) {
        SceneNode n;
        int32_t parent = -1;
        if (!readPod(file, n.gpuData) || !readPod(file, parent)) return false;
        n.parent = parent;
        n.name = std::string(shapeName(nodeType(n))) + " " + std::to_string(i);
        n.localPosition = vec3(n.gpuData.position);
        n.localRotation = vec3(n.gpuData.rotationEuler);
        n.localScale = vec3(n.gpuData.scale);
        nodes.push_back(n);
    }

    // Блок расширения (его нет в файлах старых версий)
    WorldSettings loadedWorld;
    bool hasWorld = false;
    file.seekg((std::streamoff)header.thumbnailOffset + header.thumbnailSize);
    char magic[4] = {};
    uint32_t extVersion = 0, extCount = 0;
    if (file.read(magic, 4) && strncmp(magic, "NOSX", 4) == 0 && readPod(file, extVersion) && readPod(file, extCount)
        && extVersion >= 1 && extVersion <= NOSX_VERSION && extCount == nodes.size()) {
        bool ok = true;
        for (auto& n : nodes) {
            uint8_t active = 1, isTrigger = 0, behaviorOn = 1;
            int32_t target = -1, action = 0, behavior = 0;
            ok = readString(file, n.name) && readString(file, n.meshPath)
              && readPod(file, n.localPosition) && readPod(file, n.localRotation) && readPod(file, n.localScale)
              && readPod(file, active) && readPod(file, isTrigger) && readPod(file, behaviorOn)
              && readPod(file, target) && readPod(file, action)
              && readPod(file, behavior) && readPod(file, n.behaviorVec) && readPod(file, n.behaviorSpeed)
              && readPod(file, n.lightIntensity) && readPod(file, n.lightRange);
            if (ok && extVersion >= 2) {
                int32_t bodyType = 0; uint32_t varCount = 0;
                RigidBody& b = n.body;
                ok = readPod(file, bodyType) && readPod(file, b.mass) && readPod(file, b.restitution) && readPod(file, b.friction)
                  && readPod(file, b.linearDamping) && readPod(file, b.angularDamping) && readPod(file, b.gravityScale)
                  && readString(file, n.script.path) && readPod(file, varCount) && varCount <= 256;
                b.type = std::clamp(bodyType, 0, (int)BODY_TYPE_COUNT - 1);
                for (uint32_t v = 0; ok && v < varCount; v++) {
                    ScriptVar var; int32_t type = 0, nodeRef = -1;
                    ok = readString(file, var.name) && readPod(file, type) && readPod(file, var.value) && readString(file, var.text) && readPod(file, nodeRef);
                    var.type = std::clamp(type, 0, (int)SVAR_NODE); var.node = nodeRef;
                    n.script.vars.push_back(var);
                }
            }
            if (ok && extVersion >= 3) {
                int32_t shape = 0; uint8_t flags[7] = {};
                AudioSource& a = n.audio; CharacterController& c = n.controller;
                ok = readPod(file, shape) && readPod(file, n.collider.size)
                  && readPod(file, flags[0]) && readString(file, a.clip) && readPod(file, a.volume)
                  && readPod(file, flags[1]) && readPod(file, flags[2]) && readPod(file, flags[3]) && readPod(file, a.range)
                  && readPod(file, flags[4]) && readPod(file, c.walkSpeed) && readPod(file, c.runSpeed) && readPod(file, c.jumpSpeed)
                  && readPod(file, c.radius) && readPod(file, c.height) && readPod(file, flags[5])
                  && readPod(file, n.cameraFov);
                n.collider.shape = std::clamp(shape, 0, (int)COLLIDER_COUNT - 1);
                a.enabled = flags[0]; a.loop = flags[1]; a.playOnStart = flags[2]; a.spatial = flags[3];
                c.enabled = flags[4]; c.mouseLook = flags[5];
            }
            if (ok && extVersion >= 4) {
                int32_t res = 0;
                ok = readPod(file, res) && res >= 0 && res <= 256;
                if (ok && res > 0) {
                    auto volume = std::make_shared<SdfVolume>();
                    volume->resolution = res;
                    volume->distances.resize((size_t)res * res * res);
                    file.read(reinterpret_cast<char*>(volume->distances.data()), volume->distances.size() * sizeof(float));
                    ok = (bool)file;
                    n.volume = volume;
                }
            }
            if (!ok) break;
            n.active = active != 0; n.isTrigger = isTrigger != 0; n.behaviorOn = behaviorOn != 0;
            n.triggerTarget = target;
            n.triggerAction = std::clamp(action, 0, (int)TRIGGER_ACTION_COUNT - 1);
            n.behavior = std::clamp(behavior, 0, (int)BEHAVIOR_COUNT - 1);
        }
        if (!ok) { spdlog::error("Corrupted extension block in {}", path); return false; }
        hasWorld = readPod(file, loadedWorld.sunAzimuth) && readPod(file, loadedWorld.sunElevation) && readPod(file, loadedWorld.sunIntensity)
                && readPod(file, loadedWorld.sunColor) && readPod(file, loadedWorld.skyTop) && readPod(file, loadedWorld.skyHorizon)
                && readPod(file, loadedWorld.ambient);
    }

    int count = (int)nodes.size();
    if (!hasWorld) {
        // Старый формат хранит только мировые трансформации — восстанавливаем локальные
        std::vector<mat4> worldMats;
        for (const auto& n : nodes) worldMats.push_back(composeMatrix(n.localPosition, n.localRotation, n.localScale));
        for (int i = 0; i < count; i++) {
            int p = nodes[i].parent;
            if (p < 0 || p >= count) continue;
            decomposeMatrix(glm::inverse(worldMats[p]) * worldMats[i], nodes[i].localPosition, nodes[i].localRotation, nodes[i].localScale);
        }
    }
    for (auto& n : nodes) {
        if (n.parent < -1 || n.parent >= count) n.parent = -1;
        if (n.triggerTarget < -1 || n.triggerTarget >= count) n.triggerTarget = -1;
        for (auto& v : n.script.vars) if (v.node < -1 || v.node >= count) v.node = -1;
        if (!n.meshPath.empty()) {
            // Меш мог переехать вместе со сценой (экспортированная игра) — ищем его рядом с .nos
            std::error_code ec;
            std::filesystem::path beside = std::filesystem::path(path).parent_path() / std::filesystem::path(n.meshPath).filename();
            if (!std::filesystem::exists(n.meshPath, ec) && std::filesystem::exists(beside, ec)) n.meshPath = beside.string();
            n.meshIdx = g_meshLoader ? g_meshLoader(n.meshPath) : -1;
        }
    }

    if (append) {
        int base = (int)sceneGraph.size();
        if (base + count > MAX_OBJECTS) { showToast("Not enough room in scene"); return false; }
        saveUndo();
        selectedIndices.clear();
        for (auto& n : nodes) {
            if (n.parent >= 0) n.parent += base;
            if (n.triggerTarget >= 0) n.triggerTarget += base;
            for (auto& v : n.script.vars) if (v.node >= 0) v.node += base;
            sceneGraph.push_back(n);
            selectedIndices.push_back((int)sceneGraph.size() - 1);
        }
        for (int i = base; i < (int)sceneGraph.size(); ++i) {
            int p = sceneGraph[i].parent;
            if (p >= 0) sceneGraph[p].children.push_back(i);
        }
        return true;
    }

    if (count > MAX_OBJECTS) {
        spdlog::warn("{} has {} nodes, only the first {} are loaded", path, count, MAX_OBJECTS);
        nodes.resize(MAX_OBJECTS);
        for (auto& n : nodes) {
            if (n.parent >= MAX_OBJECTS) n.parent = -1;
            if (n.triggerTarget >= MAX_OBJECTS) n.triggerTarget = -1;
            for (auto& v : n.script.vars) if (v.node >= MAX_OBJECTS) v.node = -1;
        }
    }
    sceneGraph = std::move(nodes);
    for (int i = 0; i < (int)sceneGraph.size(); ++i) {
        int p = sceneGraph[i].parent;
        if (p >= 0) sceneGraph[p].children.push_back(i);
    }
    cam.target = meta.target; cam.theta = meta.theta; cam.phi = meta.phi; cam.radius = meta.radius;
    world = hasWorld ? loadedWorld : WorldSettings();
    selectedIndices.clear(); undoStack.clear(); redoStack.clear();
    currentLoadedFile = path; lastLoadedHeader = header;
    return true;
}

bool readNosThumbnail(const std::string& path, std::vector<uint8_t>& rgba) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    NosHeader header;
    if (!readPod(file, header) || strncmp(header.magic, "NOS!", 4) != 0) return false;
    if (header.thumbnailSize != (uint32_t)(NOS_THUMB_W * NOS_THUMB_H * 4)) return false;
    rgba.resize(header.thumbnailSize);
    file.seekg(header.thumbnailOffset);
    file.read(reinterpret_cast<char*>(rgba.data()), rgba.size());
    return (bool)file;
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename);
    size_t size = (size_t)file.tellg();
    std::vector<char> buf(size);
    file.seekg(0); file.read(buf.data(), size); file.close();
    return buf;
}
