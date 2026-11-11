// Import: всё, из чего собирается игра. Внешние файлы копируются в папку проекта,
// чтобы проект можно было перенести целиком.
#include "Panels.h"
#include "Project.h"
#include "Scene.h"
#include "EngineBridge.h"
#include "FileDialogs.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
    struct Selection {
        std::string subdir, file;       // "assets", "house.nos"
        void* thumbnail = nullptr;
        int nodeCount = -1;
        int triangles = -1;
        vec3 size = {0, 0, 0};
        int lines = -1;
    };
    Selection selected;

    void select(const std::string& subdir, const std::string& file) {
        if (selected.thumbnail) destroyUiTexture(selected.thumbnail);
        selected = Selection();
        selected.subdir = subdir;
        selected.file = file;
        std::string full = project.path(subdir + "/" + file);
        std::string ext = fs::path(file).extension().string();

        if (ext == ".nos") {
            std::vector<uint8_t> pixels;
            if (readNosThumbnail(full, pixels)) selected.thumbnail = createUiTexture(pixels.data(), NOS_THUMB_W, NOS_THUMB_H);
            std::ifstream f(full, std::ios::binary);
            NosHeader header{};
            if (f.read(reinterpret_cast<char*>(&header), sizeof(header))) selected.nodeCount = (int)header.objectCount;
        } else if (ext == ".obj") {
            OBJModel model = parseOBJ(full);
            selected.triangles = (int)model.indices.size() / 3;
            vec3 lo(1e10f), hi(-1e10f);
            for (const auto& v : model.vertices) { lo = glm::min(lo, v.pos); hi = glm::max(hi, v.pos); }
            if (!model.vertices.empty()) selected.size = hi - lo;
        } else if (ext == ".nls") {
            std::ifstream f(full);
            std::string line;
            selected.lines = 0;
            while (std::getline(f, line)) selected.lines++;
        }
    }

    void fileList(const char* title, const char* subdir, const char* extension) {
        std::vector<std::string> files = project.list(subdir, extension);
        ImGui::TextDisabled("%s (%d)", title, (int)files.size());
        for (const std::string& file : files) {
            ImGui::PushID(subdir);
            bool isSelected = selected.subdir == subdir && selected.file == file;
            if (ImGui::Selectable(file.c_str(), isSelected)) select(subdir, file);
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
}

void drawImportWorkspace(GLFWwindow* window, const WorkArea& area) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar;
    float listWidth = std::min(340.0f, area.w * 0.4f);

    ImGui::SetNextWindowPos(ImVec2(area.x, area.y)); ImGui::SetNextWindowSize(ImVec2(listWidth, area.h));
    ImGui::Begin("ImportList", nullptr, flags);
    ImGui::TextDisabled("PROJECT");
    ImGui::TextWrapped("%s", project.root.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Import files...", ImVec2(-1, 0))) {
        std::string path = FileDialogs::OpenFile(window, IMPORT_FILTER);
        if (!path.empty()) {
            std::string ext = fs::path(path).extension().string();
            const char* subdir = (ext == ".nls") ? "scripts" : "assets";
            std::string imported = importIntoProject(path, subdir);
            if (imported.empty()) showToast("Import failed");
            else { select(subdir, fs::path(imported).filename().string()); rescanAssets(); showToast("Imported: " + selected.file); }
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies .obj meshes, .nos models and .nls scripts into the project");
    ImGui::Separator();
    ImGui::BeginChild("ImportFiles");
    fileList("MODELS", "assets", ".nos");
    fileList("MESHES", "assets", ".obj");
    fileList("SCENES", "scenes", ".nos");
    fileList("SCRIPTS", "scripts", ".nls");
    ImGui::EndChild();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(area.x + listWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(area.w - listWidth, area.h));
    ImGui::Begin("ImportDetails", nullptr, flags);
    if (selected.file.empty()) {
        ImGui::TextDisabled("Select a file on the left, or import one.");
        ImGui::Spacing();
        ImGui::TextWrapped("Workflow: Import brings files into the project. Model builds SDF assets. Scene assembles levels and adds physics and scripts. Script is the code editor. Export builds the game.");
        ImGui::End();
        return;
    }

    std::string rel = selected.subdir + "/" + selected.file;
    std::string full = project.path(rel);
    std::string ext = fs::path(selected.file).extension().string();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(selected.file.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s", rel.c_str());
    std::error_code ec;
    auto bytes = fs::file_size(full, ec);
    if (!ec) ImGui::Text("Size: %.1f KB", bytes / 1024.0);
    ImGui::Separator();

    if (ext == ".nos") {
        if (selected.thumbnail) ImGui::Image((ImTextureID)selected.thumbnail, ImVec2(NOS_THUMB_W * 1.5f, NOS_THUMB_H * 1.5f));
        if (selected.nodeCount >= 0) ImGui::Text("Objects: %d", selected.nodeCount);
        ImGui::Spacing();
        if (ImGui::Button("Open in Model")) { switchWorkspace(WS_MODEL); loadNos(full); }
        ImGui::SameLine();
        if (ImGui::Button("Open as Scene")) { switchWorkspace(WS_SCENE); loadNos(full); }
        ImGui::SameLine();
        if (ImGui::Button("Append to Scene")) { switchWorkspace(WS_SCENE); appendNos(full); }
    } else if (ext == ".obj") {
        ImGui::Text("Triangles: %d", selected.triangles);
        ImGui::Text("Bounds: %.2f x %.2f x %.2f", selected.size.x, selected.size.y, selected.size.z);
        ImGui::Spacing();
        if (ImGui::Button("Add to Scene")) { switchWorkspace(WS_SCENE); loadMesh(full); }
        ImGui::TextDisabled("Meshes are drawn as polygons and blend with SDF objects by depth.");
    } else if (ext == ".nls") {
        ImGui::Text("Lines: %d", selected.lines);
        ImGui::Spacing();
        if (ImGui::Button("Edit in Script")) openScriptInEditor(rel);
        ImGui::TextDisabled("Attach it to an object in Scene > Inspector > Script.");
    }
    ImGui::End();
}
