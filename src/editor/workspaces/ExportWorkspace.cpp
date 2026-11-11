// Export: сборка игры. В папку попадают плеер, шейдеры, сцена, меши и скомпилированные скрипты.
#include "Panels.h"
#include "Project.h"
#include "Settings.h"
#include "EngineBridge.h"
#include "FileDialogs.h"
#include "imgui.h"
#include <cstring>

void drawExportWorkspace(GLFWwindow* window, const WorkArea& area) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowPos(ImVec2(area.x, area.y)); ImGui::SetNextWindowSize(ImVec2(area.w, area.h));
    ImGui::Begin("ExportPanel", nullptr, flags);

    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("Export Game");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("The scene currently open in the Scene workspace becomes the game.");
    ImGui::Separator();
    ImGui::PushItemWidth(std::min(420.0f, area.w * 0.6f));

    bool changed = false;
    char nameBuf[128];
    strncpy(nameBuf, project.name.c_str(), sizeof(nameBuf)); nameBuf[sizeof(nameBuf) - 1] = 0;
    if (ImGui::InputText("Game title", nameBuf, sizeof(nameBuf))) { project.name = nameBuf; changed = true; }

    const char* presetLabel = project.exportPreset < 0 ? "Same as editor" : presetName(project.exportPreset);
    if (ImGui::BeginCombo("Quality preset", presetLabel)) {
        if (ImGui::Selectable("Same as editor", project.exportPreset < 0)) { project.exportPreset = -1; changed = true; }
        for (int p = 0; p < PRESET_CUSTOM; p++) {
            if (ImGui::Selectable(presetName(p), project.exportPreset == p)) { project.exportPreset = p; changed = true; }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Players with weak hardware can still lower it: the game reads nlsdfe.cfg next to it.");

    char dirBuf[512];
    strncpy(dirBuf, project.lastExportDir.c_str(), sizeof(dirBuf)); dirBuf[sizeof(dirBuf) - 1] = 0;
    if (ImGui::InputText("Output folder", dirBuf, sizeof(dirBuf))) { project.lastExportDir = dirBuf; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        std::string dir = FileDialogs::SelectFolder(window);
        if (!dir.empty()) { project.lastExportDir = dir; changed = true; }
    }
    ImGui::PopItemWidth();
    if (changed) saveProject();

    ImGui::Spacing();
    bool ready = !project.lastExportDir.empty();
    if (!ready) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.2f, 1.0f));
    if (ImGui::Button("Export", ImVec2(160, 36))) showToast(exportGame(project.lastExportDir) ? "Game exported" : "Export failed");
    ImGui::PopStyleColor();
    if (!ready) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextDisabled("Choose an output folder first"); }

    ImGui::Separator();
    ImGui::TextDisabled("LAST EXPORT");
    ImGui::BeginChild("ExportLog");
    for (const std::string& line : exportLog()) {
        if (line.compare(0, 5, "ERROR") == 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
        else ImGui::TextUnformatted(line.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}
