#include "Conversion.h"
#include "Scene.h"
#include "MeshConvert.h"
#include "UvTools.h"
#include "MathUtils.h"
#include "Project.h"
#include "EngineBridge.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <set>
#include <cctype>

namespace {
    bool popupRequested = false;
    ConversionKind pendingKind = CONVERT_TO_MESH;
    int meshResolution = 96;
    int volumeResolution = 64;
    bool keepOriginals = false;

    // Выделенные SDF-объекты вместе с SDF-потомками: модель из частей конвертируется целиком
    std::vector<int> sdfSelection() {
        std::set<int> nodes;
        std::vector<int> stack(selectedIndices.begin(), selectedIndices.end());
        while (!stack.empty()) {
            int idx = stack.back(); stack.pop_back();
            if (idx < 0 || idx >= (int)sceneGraph.size() || nodes.count(idx)) continue;
            if (isSdfNode(sceneGraph[idx]) && !sceneGraph[idx].isTrigger) nodes.insert(idx);
            for (int child : sceneGraph[idx].children) stack.push_back(child);
        }
        return std::vector<int>(nodes.begin(), nodes.end());
    }

    std::vector<int> meshSelection() {
        std::vector<int> out;
        for (int idx : selectedIndices) {
            if (idx < 0 || idx >= (int)sceneGraph.size()) continue;
            int mi = sceneGraph[idx].meshIdx;
            if (mi >= 0 && mi < (int)meshCache.size() && !meshCache[mi].vertices.empty()) out.push_back(idx);
        }
        return out;
    }

    std::string uniqueAssetPath(const std::string& base, const std::string& ext) {
        std::error_code ec;
        std::filesystem::create_directories(project.path("assets/meshes"), ec);
        std::string safe = base;
        for (char& c : safe) if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
        for (int i = 0; ; i++) {
            std::string path = project.path("assets/meshes/" + safe + (i ? "_" + std::to_string(i) : std::string()) + ext);
            if (!std::filesystem::exists(path, ec)) return path;
        }
    }

    // Убирает исходники (одним шагом undo) либо просто запоминает состояние для undo
    void replaceOrKeep(const std::vector<int>& originals) {
        if (keepOriginals) { saveUndo(); return; }
        selectedIndices = originals;
        deleteSelectedObject();
    }

    bool convertToMesh() {
        std::vector<int> nodes = sdfSelection();
        if (nodes.empty()) { showToast("Select SDF objects to convert"); return false; }
        updateSceneCaches();
        std::vector<GpuObject> objects;
        for (const GpuObject& o : gpuObjectCache) {
            if (std::binary_search(nodes.begin(), nodes.end(), (int)o.extra.y)) objects.push_back(o);
        }
        if (objects.empty()) { showToast("Nothing visible to convert"); return false; }

        OBJModel model;
        vec3 center;
        if (!sdfToMesh(objects, meshResolution, model, center)) { showToast("Conversion produced no surface"); return false; }
        uvProject(model.vertices, UV_BOX);   // Развёртку потом можно поправить в UV Editor

        std::string name = sceneGraph[nodes[0]].name + " Mesh";
        std::string path = uniqueAssetPath(name, ".obj");
        if (!writeOBJ(path, model.vertices)) { showToast("Cannot write " + path); return false; }
        int meshIdx = loadMeshFile(path);
        if (meshIdx < 0) { showToast("Cannot load the converted mesh"); return false; }

        replaceOrKeep(nodes);
        if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); return false; }
        SceneNode node;
        node.name = name;
        node.meshIdx = meshIdx;
        node.meshPath = path;
        node.localPosition = center;
        node.gpuData.color = vec4(1.0f, 1.0f, 1.0f, 0.5f);   // Цвета материалов уже в вершинах
        sceneGraph.push_back(node);
        selectedIndices = { (int)sceneGraph.size() - 1 };
        showToast(name + ": " + std::to_string(model.vertices.size() / 3) + " triangles");
        return true;
    }

    bool convertToVolume() {
        std::vector<int> nodes = meshSelection();
        if (nodes.empty()) { showToast("Select meshes to convert"); return false; }

        std::vector<SceneNode> created;
        for (int idx : nodes) {
            const SceneNode& source = sceneGraph[idx];
            const MeshData& mesh = meshCache[source.meshIdx];
            vec3 center;
            float half = 1.0f;
            auto volume = meshToVolume(mesh.vertices, volumeResolution, center, half);
            if (!volume) continue;

            // Куб сетки стоит там же, где был меш: мир меша * сдвиг к центру * масштаб куба
            mat4 world = calculateWorldMatrix(idx) * glm::translate(mat4(1.0f), center) * glm::scale(mat4(1.0f), vec3(half));
            mat4 local = source.parent >= 0 ? glm::inverse(calculateWorldMatrix(source.parent)) * world : world;

            vec3 average(0.0f);
            for (const Vertex& v : mesh.vertices) average += vec3(v.color);
            average /= (float)mesh.vertices.size();

            SceneNode node;
            node.name = source.name + " SDF";
            node.parent = source.parent;
            decomposeMatrix(local, node.localPosition, node.localRotation, node.localScale);
            node.gpuData.position.w = (float)SDF_VOLUME;
            node.gpuData.params = vec4(1.0f, 0.0f, 0.02f, 0.0f);
            node.gpuData.color = vec4(average * vec3(source.gpuData.color), 0.5f);
            node.volume = volume;
            created.push_back(node);
        }
        if (created.empty()) { showToast("Conversion failed"); return false; }

        // После удаления исходников индексы сдвигаются: родитель уезжает на число удалённых перед ним
        if (!keepOriginals) {
            for (SceneNode& node : created) {
                if (node.parent < 0) continue;
                int shift = 0;
                for (int removed : nodes) if (removed < node.parent) shift++;
                node.parent -= shift;
            }
        }
        replaceOrKeep(nodes);
        selectedIndices.clear();
        for (SceneNode& node : created) {
            if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); break; }
            int index = (int)sceneGraph.size();
            sceneGraph.push_back(node);
            if (node.parent >= 0) sceneGraph[node.parent].children.push_back(index);
            selectedIndices.push_back(index);
        }
        int res = volumeResolution;
        showToast("SDF volume " + std::to_string(res) + "^3 (" + std::to_string(res * res * res * 4 / 1024) + " KB each)");
        return true;
    }
}

bool runConversion(ConversionKind kind, int resolution, bool keep) {
    keepOriginals = keep;
    if (kind == CONVERT_TO_MESH) { meshResolution = resolution; return convertToMesh(); }
    volumeResolution = resolution;
    return convertToVolume();
}

bool canConvert(ConversionKind kind) {
    return kind == CONVERT_TO_MESH ? !sdfSelection().empty() : !meshSelection().empty();
}

void requestConversion(ConversionKind kind) {
    pendingKind = kind;
    popupRequested = true;
}

void drawConversionPopup() {
    const char* title = "Convert##conversion";
    if (popupRequested) { ImGui::OpenPopup(title); popupRequested = false; }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    if (pendingKind == CONVERT_TO_MESH) {
        ImGui::TextUnformatted("SDF -> Mesh");
        ImGui::TextDisabled("Selected objects and their SDF children become one polygon mesh.");
        ImGui::SliderInt("Resolution", &meshResolution, 16, 256);
        ImGui::TextDisabled("Cells along the longest side. More = finer detail, more triangles.");
    } else {
        ImGui::TextUnformatted("Mesh -> SDF Volume");
        ImGui::TextDisabled("Each selected mesh becomes a voxel distance field.\nThe mesh must be closed (watertight).");
        const int choices[] = { 32, 48, 64, 96, 128 };
        if (ImGui::BeginCombo("Resolution", (std::to_string(volumeResolution) + "^3").c_str())) {
            for (int c : choices) {
                std::string label = std::to_string(c) + "^3  (" + std::to_string(c * c * c * 4 / 1024) + " KB)";
                if (ImGui::Selectable(label.c_str(), c == volumeResolution)) volumeResolution = c;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Checkbox("Keep originals", &keepOriginals);
    ImGui::Separator();
    if (ImGui::Button("Convert", ImVec2(120, 0))) {
        if (pendingKind == CONVERT_TO_MESH) convertToMesh(); else convertToVolume();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
