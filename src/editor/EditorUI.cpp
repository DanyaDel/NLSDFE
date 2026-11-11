#include "EditorUI.h"
#include "Panels.h"
#include "Project.h"
#include "Demo.h"
#include "Common.h"
#include "Scene.h"
#include "Settings.h"
#include "Game.h"
#include "Renderer.h"
#include "EngineBridge.h"
#include "FileDialogs.h"
#include "imgui.h"
#include <numeric>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

static bool showAboutWindow = false;
static bool isFirstRunCheckDone = false;

struct ChangelogEntry {
    std::string version;
    std::string date;
    std::vector<std::string> changes;
    ChangelogEntry(std::string v, std::string d, std::vector<std::string> c)
        : version(v), date(d), changes(c) {}
};

const std::vector<ChangelogEntry> appChangelog = {
    ChangelogEntry("v1.3.0 - Potato Update", "10.11.2026", {
        "Runs on Vulkan 1.0 hardware, no optional GPU features required",
        "Per-ray object culling, render-on-demand and dynamic resolution",
        "Graphics settings with quality presets (Potato .. Ultra)",
        "New shapes: Cylinder, Torus, Capsule, Cone; CSG operation in Inspector",
        "Point lights, procedural sky, floor grid with snapping",
        "Play Mode (F5): first-person player, SDF collisions, triggers, behaviors",
        "Asset browser, material presets, Redo, 100-step history",
        "File dialogs on Linux, scene files keep names and hierarchy"
    }),
    ChangelogEntry("v1.2.1 - Optimization & Quality", "02.06.2026", {
        "Implemented Bounding Sphere optimization (significant FPS boost)",
        "Increased Raymarching steps from 32 to 128 (Ultra Quality)",
        "Improved Shadow precision and resolution",
        "Finer hit-threshold for smooth edges"
    }),
    ChangelogEntry("v1.2.0 - Core Modernization", "02.06.2026", {
        "Integrated GLM for high-performance mathematics",
        "Integrated spdlog for professional terminal logging",
        "Integrated VMA (Vulkan Memory Allocator) for stable GPU memory management",
        "Fixed hardcoded resource paths for improved portability",
        "Major architectural refactoring (Renderer extraction)"
    }),
    ChangelogEntry("v1.1.0 - Hierarchy Fix", "10.01.2026", {
        "Added Drag & Drop grouping",
        "Fixed double-transform on parent/child selection",
        "Added 'Unparent' area"
    })
};

// --- Действия ---
// =================================================================
void actionSaveSceneAs(GLFWwindow* window) {
    std::string path = FileDialogs::SaveFile(window, NOS_FILTER, documentDirectory());
    if (path.empty()) return;
    if (path.find(".nos") == std::string::npos) path += ".nos";
    saveNos(path);
}

void actionSaveScene(GLFWwindow* window) {
    if (currentLoadedFile.empty() || currentLoadedFile == "Untitled") actionSaveSceneAs(window);
    else saveNos(currentLoadedFile);
}

void actionOpenScene(GLFWwindow* window) {
    std::string path = FileDialogs::OpenFile(window, NOS_FILTER);
    if (!path.empty()) loadNos(path);
}

void actionPlay(GLFWwindow* window) {
    if (currentWorkspace != WS_SCENE) switchWorkspace(WS_SCENE);
    startPlay(window);
}

void actionSelectAll() {
    selectedIndices.resize(sceneGraph.size());
    std::iota(selectedIndices.begin(), selectedIndices.end(), 0);
}

// --- Окна ---
// =================================================================
void renderAboutWindow() {
    if (!showAboutWindow) return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = ImVec2(io.DisplaySize.x * 0.9f, io.DisplaySize.y * 0.9f);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) / 2.0f, (io.DisplaySize.y - size.y) / 2.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    if (ImGui::Begin("About Engine", &showAboutWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Dummy(ImVec2(0, 50));
        ImGui::SetWindowFontScale(2.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28f, 0.56f, 0.90f, 1.0f));
        TextCentered("NEXT LEVEL SDF ENGINE");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 10));
        TextCentered("Potato Update " + ENGINE_VERSION);
        ImGui::Dummy(ImVec2(0, 10));
        TextCentered("GPU: " + renderStats.gpuName);

        float buttonW = 200;
        float buttonH = 40;
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - buttonH - 30);
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - buttonW) * 0.5f);
        if (ImGui::Button("Close", ImVec2(buttonW, buttonH))) showAboutWindow = false;
    }
    ImGui::End();
}

void renderChangelogWindow() {
    if (!showChangelog) return;
    ImGui::SetNextWindowSize(ImVec2(460, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("What's New?", &showChangelog, ImGuiWindowFlags_NoCollapse)) {
        for (const auto& entry : appChangelog) {
            ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "%s", entry.version.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("(%s)", entry.date.c_str());
            for (const auto& point : entry.changes) { ImGui::Bullet(); ImGui::TextWrapped("%s", point.c_str()); }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }
        if (ImGui::Button("Close")) showChangelog = false;
    }
    ImGui::End();
}

static void renderSettingsWindow() {
    if (!showSettingsWindow) return;
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    bool wasOpen = showSettingsWindow;
    bool changed = false;
    if (ImGui::Begin("Settings", &showSettingsWindow, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("SettingsTabs")) {
            if (ImGui::BeginTabItem("Graphics")) {
                ImGui::TextDisabled("%s", renderStats.gpuName.c_str());
                ImGui::Spacing();

                if (ImGui::BeginCombo("Quality preset", presetName(gfx.preset))) {
                    for (int p = 0; p < PRESET_CUSTOM; p++) {
                        if (ImGui::Selectable(presetName(p), gfx.preset == p)) { applyPreset(p); changed = true; }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Separator();

                // Любая ручная правка переводит пресет в Custom
                bool custom = false;
                float scalePct = gfx.renderScale * 100.0f;
                if (ImGui::SliderFloat("Render scale", &scalePct, 20.0f, 100.0f, "%.0f%%")) { gfx.renderScale = scalePct / 100.0f; custom = true; }
                custom |= ImGui::Checkbox("Dynamic resolution while moving", &gfx.autoScale);
                if (gfx.autoScale) {
                    custom |= ImGui::SliderInt("Target FPS", &gfx.targetFps, 15, 240);
                } else {
                    float movePct = gfx.interactiveScale * 100.0f;
                    if (ImGui::SliderFloat("Scale while moving", &movePct, 20.0f, 100.0f, "%.0f%%")) { gfx.interactiveScale = movePct / 100.0f; custom = true; }
                }
                custom |= ImGui::Checkbox("Solid shading while moving", &gfx.simplifyOnInteract);
                ImGui::Separator();
                custom |= ImGui::SliderInt("Raymarch steps", &gfx.maxSteps, 16, 256);
                const char* shadowModes[] = { "Off", "Hard", "Soft" };
                custom |= ImGui::Combo("Shadows", &gfx.shadowMode, shadowModes, 3);
                custom |= ImGui::SliderInt("Shadow steps", &gfx.shadowSteps, 4, 96);
                custom |= ImGui::Checkbox("Point light shadows", &gfx.lightShadows);
                custom |= ImGui::Checkbox("Ambient occlusion", &gfx.ao);
                int reflections = gfx.bounces - 1;
                if (ImGui::SliderInt("Reflection bounces", &reflections, 0, 3)) { gfx.bounces = reflections + 1; custom = true; }
                if (custom) { gfx.preset = PRESET_CUSTOM; changed = true; }

                ImGui::Separator();
                if (ImGui::Checkbox("VSync", &gfx.vsync)) { gfxSwapchainDirty = true; changed = true; }
                changed |= ImGui::Checkbox("Sleep when idle", &gfx.idleThrottle);
                changed |= ImGui::InputInt("GPU index (-1 = auto, restart)", &gfx.gpuIndex);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Editor")) {
                changed |= ImGui::Checkbox("Floor grid", &editorCfg.showGrid);
                changed |= ImGui::Checkbox("Snap to grid", &editorCfg.snap);
                changed |= ImGui::DragFloat("Snap step", &editorCfg.snapStep, 0.05f, 0.05f, 10.0f, "%.2f m");
                changed |= ImGui::DragFloat("Rotation snap", &editorCfg.snapAngleDeg, 1.0f, 1.0f, 90.0f, "%.0f deg");
                changed |= ImGui::SliderFloat("Camera FOV", &editorCfg.fovDeg, 20.0f, 110.0f, "%.0f deg");
                changed |= ImGui::Checkbox("Show stats overlay", &editorCfg.showStats);
                ImGui::Separator();
                ImGui::TextDisabled("Project folder");
                ImGui::TextWrapped("%s", project.root.c_str());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("World")) {
                ImGui::TextDisabled("Saved with the scene");
                ImGui::SliderAngle("Sun azimuth", &world.sunAzimuth, -180.0f, 180.0f);
                ImGui::SliderAngle("Sun elevation", &world.sunElevation, 0.0f, 90.0f);
                ImGui::SliderFloat("Sun intensity", &world.sunIntensity, 0.0f, 10.0f);
                ImGui::ColorEdit3("Sun color", &world.sunColor.x);
                ImGui::ColorEdit3("Sky top", &world.skyTop.x);
                ImGui::ColorEdit3("Sky horizon", &world.skyHorizon.x);
                ImGui::SliderFloat("Ambient light", &world.ambient, 0.0f, 2.0f);
                if (ImGui::Button("Reset world")) world = WorldSettings();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls")) {
                const char* keys[][2] = {
                    {"W / A / S / D, Q / E", "Move camera (Shift = faster)"},
                    {"RMB drag", "Orbit"}, {"MMB drag / Shift + RMB", "Pan"}, {"Wheel", "Zoom"},
                    {"LMB / LMB drag", "Select / box select (Shift adds)"},
                    {"1 / 2 / 3", "Move / Rotate / Scale gizmo"},
                    {"Ctrl (while dragging)", "Toggle snapping"},
                    {"F1 .. F4", "Wire / Solid / Material / Rendered"},
                    {"F or Numpad .", "Focus selection"},
                    {"Alt + D", "Duplicate"}, {"X / Delete", "Delete"}, {"H", "Hide / show"},
                    {"Ctrl + C / V", "Copy / paste"},
                    {"Ctrl + Z, Ctrl + Y", "Undo / redo"},
                    {"Ctrl + A, Alt + A", "Select all / none"},
                    {"Ctrl + N / O / S", "New / open / save scene"},
                    {"G / T / N", "Grid / toolbar / sidebar"},
                    {"F5", "Play Mode (Esc to stop)"},
                };
                if (ImGui::BeginTable("Keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    for (const auto& k : keys) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "%s", k[0]);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(k[1]);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    // Пишем на диск, когда пользователь отпустил ползунок или закрыл окно
    static bool dirty = false;
    dirty |= changed;
    if (dirty && (!ImGui::IsAnyItemActive() || (wasOpen && !showSettingsWindow))) { saveSettings(settingsPath()); dirty = false; }
}


// --- Главный кадр интерфейса ---
// =================================================================
void beginUIFrame() {
    uiNewFrame();
    setupImGuiStyle();
}

bool uiNeedsAnimation() { return toast.active; }

void renderUI(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();

    if (isPlayMode()) { drawPlayHud(); ImGui::Render(); return; }

    if (!isFirstRunCheckDone) {
        if (!std::filesystem::exists("imgui.ini")) showAboutWindow = true;
        if (editorCfg.lastVersion != ENGINE_VERSION) {
            showChangelog = true;
            editorCfg.lastVersion = ENGINE_VERSION;
            saveSettings(settingsPath());
        }
        isFirstRunCheckDone = true;
    }

    bool editing = currentWorkspace == WS_MODEL || currentWorkspace == WS_SCENE;
    const ImVec4 accent(0.28f, 0.56f, 0.90f, 1.0f);

    float menuHeight = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        menuHeight = ImGui::GetWindowSize().y;

        // Рабочие пространства — слева направо по ходу работы над игрой
        for (int ws = 0; ws < WS_COUNT; ws++) {
            bool current = currentWorkspace == ws;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(workspaceName((Workspace)ws))) switchWorkspace((Workspace)ws);
            if (current) ImGui::PopStyleColor();
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(editingModel() ? "New Model" : "New Scene", "Ctrl+N")) newDocument();
            if (ImGui::MenuItem("Load Demo Scene")) { switchWorkspace(WS_SCENE); loadDemoScene(); }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) actionOpenScene(window);
            if (ImGui::MenuItem("Append...")) { std::string path = FileDialogs::OpenFile(window, NOS_FILTER); if (!path.empty()) appendNos(path); }
            if (ImGui::MenuItem("Save", "Ctrl+S")) actionSaveScene(window);
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) actionSaveSceneAs(window);
            ImGui::Separator();
            if (ImGui::MenuItem("Open Project Folder...")) {
                std::string dir = FileDialogs::SelectFolder(window);
                if (!dir.empty() && openProject(dir)) { rescanAssets(); saveSettings(settingsPath()); showToast("Project: " + project.name); }
            }
            if (ImGui::MenuItem("Settings...")) showSettingsWindow = true;
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Changelog")) showChangelog = true;
                if (ImGui::MenuItem("About Engine")) showAboutWindow = true;
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) { glfwSetWindowShouldClose(window, true); }
            ImGui::EndMenu();
        }

        if (editing) {
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) { performUndo(); }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty())) { performRedo(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate", "Alt+D")) { duplicateSelectedObject(); }
                if (ImGui::MenuItem("Delete", "X")) { deleteSelectedObject(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Select All", "Ctrl+A")) actionSelectAll();
                if (ImGui::MenuItem("Select None", "Alt+A")) { selectedIndices.clear(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Snap to Grid", nullptr, &editorCfg.snap)) saveSettings(settingsPath());
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Add")) { addMenuItems(window); ImGui::EndMenu(); }

            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Toolbar", "T", &showToolbar);
                ImGui::MenuItem("Sidebar", "N", &showSidebar);
                ImGui::MenuItem("Assets", nullptr, &showAssetBrowser);
                ImGui::Separator();
                if (ImGui::MenuItem("Floor Grid", "G", &editorCfg.showGrid)) saveSettings(settingsPath());
                ImGui::MenuItem("Gizmos", nullptr, &showGizmos);
                if (ImGui::MenuItem("Stats", nullptr, &editorCfg.showStats)) saveSettings(settingsPath());
                ImGui::EndMenu();
            }

            float buttonWidth = 25.0f;
            float totalWidth = 4 * buttonWidth + 4 * ImGui::GetStyle().ItemSpacing.x + 60.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - totalWidth - 20.0f);
            if (currentWorkspace == WS_SCENE) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.2f, 1.0f));
                if (ImGui::Button("Play", ImVec2(60.0f, 0))) actionPlay(window);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Mode (F5)");
            } else {
                ImGui::Dummy(ImVec2(60.0f, 0));
            }
            ImGui::SameLine();
            const char* labels[] = {"W", "S", "M", "R"};
            const char* tips[] = {"Wireframe / heatmap (F1)", "Solid (F2)", "Material (F3)", "Rendered (F4)"};
            for(int i=0; i<4; i++) {
                bool current = currentShadingMode == i;
                if (current) ImGui::PushStyleColor(ImGuiCol_Button, accent);
                if (ImGui::Button(labels[i], ImVec2(buttonWidth, 0))) currentShadingMode = i;
                if (current) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
                if(i < 3) ImGui::SameLine();
            }
        }
        ImGui::EndMainMenuBar();
    }

    WorkArea area = { 0.0f, menuHeight, io.DisplaySize.x, std::max(io.DisplaySize.y - menuHeight, 1.0f) };
    // Вкладки без 3D-вида вьюпорт не занимают — и SDF-кадр для них не считается
    if (!editing) viewportRect = vec4(0.0f);
    switch (currentWorkspace) {
        case WS_IMPORT: drawImportWorkspace(window, area); break;
        case WS_MODEL: drawModelWorkspace(window, area); break;
        case WS_SCENE: drawSceneWorkspace(window, area); break;
        case WS_SCRIPT: drawScriptWorkspace(window, area); break;
        case WS_EXPORT: drawExportWorkspace(window, area); break;
        default: break;
    }
    drawToast();

    renderSettingsWindow();
    renderChangelogWindow();
    renderAboutWindow();
    ImGui::Render();
}

void setupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(6, 6);
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 1.0f;
    ImVec4* colors = style.Colors;
    ImVec4 greyMedium   = ImVec4(0.18f, 0.18f, 0.18f, guiAlpha);
    ImVec4 bgAlpha      = ImVec4(0.11f, 0.11f, 0.11f, guiAlpha);
    ImVec4 textWhite    = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    ImVec4 accentBlue   = ImVec4(0.28f, 0.56f, 0.90f, 1.00f);
    colors[ImGuiCol_WindowBg]       = bgAlpha;
    colors[ImGuiCol_Button]         = greyMedium;
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
    colors[ImGuiCol_HeaderActive]   = accentBlue;
    colors[ImGuiCol_Text]           = textWhite;
    colors[ImGuiCol_TextSelectedBg] = accentBlue;
    colors[ImGuiCol_MenuBarBg]      = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
}

