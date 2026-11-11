// UV Editor: развёртка импортированных мешей. Слева — развёртка в квадрате 0..1,
// справа — модель с шахматкой, по которой видны растяжения.
// SDF-формам UV не нужны: у них нет полигонов.
#include "Panels.h"
#include "Project.h"
#include "Scene.h"
#include "UvTools.h"
#include "EngineBridge.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>

namespace {
    float zoom = 1.0f;
    vec2 pan = {0.0f, 0.0f};
    vec2 offset = {0.0f, 0.0f};
    vec2 scale = {1.0f, 1.0f};
    float rotation = 0.0f;

    // Меш выделенного узла; -1, если выделен не меш
    int selectedMeshNode() {
        for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it) {
            int idx = *it;
            if (idx >= 0 && idx < (int)sceneGraph.size() && sceneGraph[idx].meshIdx >= 0 && sceneGraph[idx].meshIdx < (int)meshCache.size()) return idx;
        }
        return -1;
    }

    void drawCanvas(const MeshData* mesh) {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 16 || avail.y < 16) return;
        ImGui::InvisibleButton("uvcanvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemHovered()) {
            if (io.MouseWheel != 0.0f) zoom = std::clamp(zoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.2f, 20.0f);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) pan += vec2(io.MouseDelta.x, io.MouseDelta.y);
        }

        float side = std::min(avail.x, avail.y) * 0.85f * zoom;
        vec2 center = vec2(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f) + pan;
        // V растёт вверх, как принято в UV-редакторах
        auto toScreen = [&](vec2 uv) { return ImVec2(center.x + (uv.x - 0.5f) * side, center.y - (uv.y - 0.5f) * side); };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
        dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(24, 24, 28, 255));
        for (int i = 0; i <= 8; i++) {
            float t = i / 8.0f;
            ImU32 col = (i == 0 || i == 8) ? IM_COL32(200, 200, 200, 255) : IM_COL32(70, 70, 78, 255);
            dl->AddLine(toScreen({t, 0}), toScreen({t, 1}), col);
            dl->AddLine(toScreen({0, t}), toScreen({1, t}), col);
        }
        if (mesh) {
            // Слишком плотные меши рисуем с прореживанием, чтобы интерфейс не тормозил
            size_t triangles = mesh->vertices.size() / 3;
            size_t step = std::max<size_t>(1, triangles / 30000);
            for (size_t t = 0; t < triangles; t += step) {
                ImVec2 a = toScreen(mesh->vertices[t * 3].uv), b = toScreen(mesh->vertices[t * 3 + 1].uv), c = toScreen(mesh->vertices[t * 3 + 2].uv);
                dl->AddTriangleFilled(a, b, c, IM_COL32(70, 140, 230, 28));
                dl->AddTriangle(a, b, c, IM_COL32(120, 190, 255, 200));
            }
        }
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6), IM_COL32(200, 200, 200, 200), "Wheel: zoom   Right / middle drag: pan");
        dl->PopClipRect();
    }
}

void drawUvWorkspace(GLFWwindow* window, const WorkArea& area) {
    (void)window;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar;
    float toolsWidth = std::min(290.0f, area.w * 0.25f);
    float canvasWidth = (area.w - toolsWidth) * 0.5f;
    viewportRect = vec4(area.x + canvasWidth, area.y, area.w - canvasWidth - toolsWidth, area.h);

    int nodeIdx = selectedMeshNode();
    MeshData* mesh = nodeIdx >= 0 ? &meshCache[sceneGraph[nodeIdx].meshIdx] : nullptr;
    // Шахматка видна только на меше, с которым сейчас работают
    for (auto& m : meshCache) if (&m != mesh) m.showChecker = false;

    ImGui::SetNextWindowPos(ImVec2(area.x, area.y)); ImGui::SetNextWindowSize(ImVec2(canvasWidth, area.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("UvCanvas", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(mesh);
    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(ImVec2(area.x + area.w - toolsWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(toolsWidth, area.h));
    ImGui::Begin("UvTools", nullptr, flags);
    ImGui::TextDisabled("UV EDITOR");
    ImGui::Separator();
    if (!mesh) {
        ImGui::TextWrapped("Select a polygon mesh in the 3D view.");
        ImGui::Spacing();
        ImGui::TextWrapped("Meshes come from File > Import > Mesh (.obj). SDF shapes have no polygons and need no UVs.");
        ImGui::Spacing();
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            if (sceneGraph[i].meshIdx < 0) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(sceneGraph[i].name.c_str())) selectedIndices = { i };
            ImGui::PopID();
        }
        ImGui::End();
        drawViewportOverlays();
        return;
    }

    SceneNode& node = sceneGraph[nodeIdx];
    ImGui::TextUnformatted(node.name.c_str());
    ImGui::Text("Triangles: %d", (int)mesh->vertices.size() / 3);
    ImGui::Checkbox("Checker preview", &mesh->showChecker);
    ImGui::Separator();

    bool changed = false;
    ImGui::TextDisabled("PROJECT");
    for (int p = 0; p < UV_PROJECTION_COUNT; p++) {
        if (ImGui::Button(uvProjectionName(p), ImVec2((toolsWidth - 30) * 0.5f, 0))) { uvProject(mesh->vertices, p); changed = true; mesh->showChecker = true; }
        if (p % 2 == 0) ImGui::SameLine();
    }
    ImGui::Separator();

    ImGui::TextDisabled("TRANSFORM");
    ImGui::DragFloat2("Offset", &offset.x, 0.01f);
    ImGui::DragFloat2("Scale", &scale.x, 0.01f, 0.01f, 100.0f);
    ImGui::DragFloat("Rotation", &rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
    if (ImGui::Button("Apply")) {
        uvTransform(mesh->vertices, offset, scale, rotation);
        offset = vec2(0.0f); scale = vec2(1.0f); rotation = 0.0f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit to 0..1")) { uvFit(mesh->vertices); changed = true; }
    if (ImGui::Button("Flip U")) { uvTransform(mesh->vertices, vec2(0.0f), vec2(-1.0f, 1.0f), 0.0f); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Flip V")) { uvTransform(mesh->vertices, vec2(0.0f), vec2(1.0f, -1.0f), 0.0f); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Rotate 90")) { uvTransform(mesh->vertices, vec2(0.0f), vec2(1.0f), 90.0f); changed = true; }
    ImGui::Separator();

    ImGui::TextDisabled("FILE");
    if (ImGui::Button("Reload from file")) {
        OBJModel model = parseOBJ(mesh->path);
        if (model.vertices.size() == mesh->vertices.size()) { mesh->vertices = model.vertices; changed = true; }
        else showToast("File changed too much to reload");
    }
    if (ImGui::Button("Save mesh to project")) {
        // Правленый меш сохраняется в проект; исходный файл не трогаем
        std::error_code ec;
        std::filesystem::path source(mesh->path);
        std::filesystem::path target = std::filesystem::path(project.path("assets")) / source.filename();
        bool insideProject = std::filesystem::equivalent(source.parent_path(), target.parent_path(), ec);
        if (!insideProject) target = std::filesystem::path(project.path("assets")) / (source.stem().string() + "_uv.obj");
        if (writeOBJ(target.string(), mesh->vertices)) {
            mesh->path = target.string();
            for (auto& n : sceneGraph) if (n.meshIdx == node.meshIdx) n.meshPath = mesh->path;
            showToast("Saved: " + target.filename().string());
        } else showToast("Cannot write " + target.string());
    }
    ImGui::TextDisabled("Texture images are not supported yet:\nUVs are saved for use in other tools.");
    ImGui::End();

    if (changed) updateMesh(node.meshIdx);
    drawViewportOverlays();
}
