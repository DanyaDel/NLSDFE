#include "EditorUI.h"
#include "Panels.h"
#include "Project.h"
#include "Demo.h"
#include "Conversion.h"
#include "BlenderLink.h"
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
#include <cctype>
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
    ChangelogEntry("v1.5.0 - Conversion, Blender, Visual Scripts", "11.11.2026", {
        "Object > Convert to Mesh: SDF models become polygon meshes with colours and UVs",
        "Object > Convert to SDF Volume: meshes become voxel SDF shapes with CSG and physics",
        "Object > Edit in Blender: live link, Ctrl+S in Blender sends changes back",
        "Visual scripting: node graphs compiled to NLS, every engine function as a node",
        "Fixed: the UI did not react to the mouse"
    }),
    ChangelogEntry("v1.4.0 - Game Engine", "11.11.2026", {
        "Engine split into modules; separate player executable for exported games",
        "Editor modes: Model, Scene, UV and Script editors; import and export in the File menu",
        "Components: Collider, Rigid Body, Character Controller, Audio Source, Script, Behavior",
        "Rigid body physics on SDF colliders",
        "NLS: the engine's own compiled scripting language with a built-in editor",
        "Camera objects: Play Mode (F5) looks through the scene camera",
        "UV editor for imported meshes; project folders; demo scene"
    }),
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
        TextCentered("Game Engine " + ENGINE_VERSION);
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
                changed |= ImGui::SliderFloat("Interface scale (restart)", &editorCfg.uiScale, 0.75f, 2.5f, "%.2f");
                char blenderBuf[512];
                strncpy(blenderBuf, editorCfg.blenderPath.c_str(), sizeof(blenderBuf)); blenderBuf[sizeof(blenderBuf) - 1] = 0;
                if (ImGui::InputText("Blender executable", blenderBuf, sizeof(blenderBuf))) { editorCfg.blenderPath = blenderBuf; changed = true; }
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

bool uiNeedsAnimation() { return toast.active || editorPreviewActive(); }

// --- File > Import: внешний файл копируется в проект и сразу идёт в дело ---
static void importMesh(GLFWwindow* window) {
    std::string path = FileDialogs::OpenFile(window, OBJ_FILTER);
    if (path.empty()) return;
    std::string imported = importIntoProject(path, "assets");
    if (imported.empty()) { showToast("Import failed"); return; }
    if (!workspaceHasViewport(currentWorkspace)) switchWorkspace(WS_SCENE);
    loadMesh(imported);
}

static void importModel(GLFWwindow* window) {
    std::string path = FileDialogs::OpenFile(window, NOS_FILTER);
    if (path.empty()) return;
    std::string imported = importIntoProject(path, "assets");
    if (imported.empty()) { showToast("Import failed"); return; }
    rescanAssets();
    if (!workspaceHasViewport(currentWorkspace)) switchWorkspace(WS_SCENE);
    appendNos(imported);
}

static void importScript(GLFWwindow* window) {
    std::string path = FileDialogs::OpenFile(window, SCRIPT_FILTER);
    if (path.empty()) return;
    std::string imported = importIntoProject(path, "scripts");
    if (imported.empty()) { showToast("Import failed"); return; }
    openScriptInEditor(project.relative(imported));
}

static void importSound(GLFWwindow* window) {
    std::string path = FileDialogs::OpenFile(window, SOUND_FILTER);
    if (path.empty()) return;
    std::string imported = importIntoProject(path, "assets/sounds");
    showToast(imported.empty() ? "Import failed" : "Sound imported: choose it in an Audio Source component");
}

static void exportScreenshot(GLFWwindow* window) {
    std::string path = FileDialogs::SaveFile(window, "PPM Image (*.ppm)\0*.ppm\0", project.root);
    if (path.empty()) return;
    if (path.find(".ppm") == std::string::npos) path += ".ppm";
    showToast(saveScreenshot(path) ? "Screenshot saved" : "Screenshot failed");
}

// Верхняя строка: меню приложения
static float drawMainMenu(GLFWwindow* window, bool editing) {
    float height = 0.0f;
    if (!ImGui::BeginMainMenuBar()) return height;
    height = ImGui::GetWindowSize().y;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(editingModel() ? "New Model" : "New Scene", "Ctrl+N")) newDocument();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) actionOpenScene(window);
        if (ImGui::MenuItem("Save", "Ctrl+S")) actionSaveScene(window);
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) actionSaveSceneAs(window);
        if (ImGui::MenuItem("Append...")) { std::string path = FileDialogs::OpenFile(window, NOS_FILTER); if (!path.empty()) appendNos(path); }
        ImGui::Separator();
        if (ImGui::BeginMenu("Import")) {
            if (ImGui::MenuItem("Mesh (.obj)...")) importMesh(window);
            if (ImGui::MenuItem("Model or Scene (.nos)...")) importModel(window);
            if (ImGui::MenuItem("Script (.nls)...")) importScript(window);
            if (ImGui::MenuItem("Sound (.wav, .mp3, .flac)...")) importSound(window);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Export")) {
            if (ImGui::MenuItem("Game...")) { if (editingModel()) switchWorkspace(WS_SCENE); showExportWindow = true; }
            if (ImGui::MenuItem("Screenshot (.ppm)...")) exportScreenshot(window);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open Project Folder...")) {
            std::string dir = FileDialogs::SelectFolder(window);
            if (!dir.empty() && openProject(dir)) { rescanAssets(); saveSettings(settingsPath()); showToast("Project: " + project.name); }
        }
        if (ImGui::MenuItem("Load Demo Scene")) { switchWorkspace(WS_SCENE); loadDemoScene(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) { glfwSetWindowShouldClose(window, true); }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editing && !undoStack.empty())) { performUndo(); }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editing && !redoStack.empty())) { performRedo(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Alt+D", false, editing)) { duplicateSelectedObject(); }
        if (ImGui::MenuItem("Delete", "X", false, editing)) { deleteSelectedObject(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences...")) showSettingsWindow = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Render")) {
        const char* names[] = { "Wireframe / Heatmap", "Solid", "Material Preview", "Rendered" };
        const char* keys[] = { "F1", "F2", "F3", "F4" };
        for (int i = 0; i < 4; i++) if (ImGui::MenuItem(names[i], keys[i], currentShadingMode == i)) currentShadingMode = i;
        ImGui::Separator();
        if (ImGui::BeginMenu("Quality")) {
            for (int p = 0; p < PRESET_CUSTOM; p++) {
                if (ImGui::MenuItem(presetName(p), nullptr, gfx.preset == p)) { applyPreset(p); saveSettings(settingsPath()); }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Play", "F5")) actionPlay(window);
        if (ImGui::MenuItem("Save Screenshot...")) exportScreenshot(window);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Toolbar", "T", &showToolbar);
        ImGui::MenuItem("Sidebar", "N", &showSidebar);
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser);
        if (ImGui::MenuItem("Stats Overlay", nullptr, &editorCfg.showStats)) saveSettings(settingsPath());
        ImGui::Separator();
        ImGui::MenuItem("Export Game", nullptr, &showExportWindow);
        ImGui::MenuItem("Preferences", nullptr, &showSettingsWindow);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls")) showSettingsWindow = true;
        if (ImGui::MenuItem("What's New")) showChangelog = true;
        if (ImGui::MenuItem("About")) showAboutWindow = true;
        ImGui::EndMenu();
    }

    // Справа: проект и открытый файл
    std::string title = project.name + "  |  " + std::filesystem::path(currentLoadedFile).filename().string();
    float width = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextDisabled("%s", title.c_str());

    ImGui::EndMainMenuBar();
    return height;
}

// Вторая строка: режимы редактора и меню текущего вида
// Object > Save as Prefab: выделенный узел с потомками уходит в assets/prefabs/<имя>.nos
static void saveSelectionAsPrefab() {
    if (selectedIndices.size() != 1 || selectedIndices[0] >= (int)sceneGraph.size()) return;
    std::vector<SceneNode> nodes = copySubtree(selectedIndices[0]);
    // Корень префаба встаёт в начало координат; ссылки на узлы вне префаба в файл не попадают
    nodes[0].localPosition = vec3(0.0f);
    for (auto& n : nodes) {
        if (n.triggerTarget < -1) n.triggerTarget = -1;
        for (auto& v : n.script.vars) if (v.node < -1) v.node = -1;
    }
    std::string name = nodes[0].name.empty() ? "prefab" : nodes[0].name;
    for (char& c : name) if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
    std::error_code ec;
    std::filesystem::create_directories(project.path("assets/prefabs"), ec);
    std::string file = project.path("assets/prefabs/" + name + ".nos");

    // saveScene пишет текущую сцену — на время записи подставляем префаб
    std::vector<SceneNode> keep = std::move(sceneGraph);
    std::string keepFile = currentLoadedFile;
    sceneGraph = nodes;
    for (int i = 0; i < (int)sceneGraph.size(); i++) if (sceneGraph[i].parent >= 0) sceneGraph[sceneGraph[i].parent].children.push_back(i);
    bool ok = saveScene(file, {});
    sceneGraph = std::move(keep);
    currentLoadedFile = keepFile;
    rescanAssets();
    showToast(ok ? "Prefab saved: " + name + "  (scene.instantiate(\"" + name + "\", position))" : "Cannot write " + file);
}

static float drawHeaderBar(GLFWwindow* window, float y, bool editing) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec4 accent(0.28f, 0.56f, 0.90f, 1.0f);
    const float height = ImGui::GetFrameHeight() + 10.0f;

    ImGui::SetNextWindowPos(ImVec2(0, y)); ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
    ImGui::Begin("HeaderBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        // Режимы
        for (int ws = 0; ws < WS_COUNT; ws++) {
            bool current = currentWorkspace == ws;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(workspaceName((Workspace)ws))) switchWorkspace((Workspace)ws);
            if (current) ImGui::PopStyleColor();
        }

        if (editing) {
            ImGui::TextDisabled("|");
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Floor Grid", "G", &editorCfg.showGrid)) saveSettings(settingsPath());
                ImGui::MenuItem("Gizmos", nullptr, &showGizmos);
                if (ImGui::MenuItem("Focus Selection", "F", false, !selectedIndices.empty())) cam.target = getSelectionCenter();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Select")) {
                if (ImGui::MenuItem("All", "Ctrl+A")) actionSelectAll();
                if (ImGui::MenuItem("None", "Alt+A")) selectedIndices.clear();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add")) { addMenuItems(window); ImGui::EndMenu(); }
            if (ImGui::BeginMenu("Object")) {
                if (ImGui::MenuItem("Duplicate", "Alt+D")) duplicateSelectedObject();
                if (ImGui::MenuItem("Delete", "X")) deleteSelectedObject();
                ImGui::Separator();
                if (ImGui::MenuItem("Edit in Blender", nullptr, false, canEditInBlender())) editInBlender();
                ImGui::Separator();
                if (ImGui::MenuItem("Convert to Mesh...", nullptr, false, canConvert(CONVERT_TO_MESH))) requestConversion(CONVERT_TO_MESH);
                if (ImGui::MenuItem("Convert to SDF Volume...", nullptr, false, canConvert(CONVERT_TO_VOLUME))) requestConversion(CONVERT_TO_VOLUME);
                ImGui::Separator();
                if (ImGui::MenuItem("Save as Prefab", nullptr, false, selectedIndices.size() == 1)) saveSelectionAsPrefab();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes the object with its children and components to assets/prefabs/.\nScripts create copies with scene.instantiate(\"name\", position)");
                ImGui::Separator();
                if (ImGui::MenuItem("Hide / Show", "H", false, !selectedIndices.empty())) {
                    saveUndo();
                    for (int idx : selectedIndices) if (idx < (int)sceneGraph.size()) sceneGraph[idx].active = !sceneGraph[idx].active;
                }
                ImGui::EndMenu();
            }

            ImGui::TextDisabled("|");
            auto toggle = [&](const char* label, bool on, const char* tip) {
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, accent);
                bool clicked = ImGui::Button(label);
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                return clicked;
            };
            if (toggle("Snap", editorCfg.snap, "Snap to grid while dragging (Ctrl inverts)")) { editorCfg.snap = !editorCfg.snap; saveSettings(settingsPath()); }
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::DragFloat("##snapstep", &editorCfg.snapStep, 0.05f, 0.05f, 10.0f, "%.2f m") && !ImGui::IsItemActive()) saveSettings(settingsPath());

            if (currentWorkspace == WS_MODEL) {
                ImGui::TextDisabled("|");
                bool can = canEditInBlender();
                if (!can) ImGui::BeginDisabled();
                if (ImGui::Button("Edit in Blender")) editInBlender();
                if (!can) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Open this model in Blender; saving there (Ctrl+S) sends it back");
            }

            // Справа: запуск и режим затенения
            const float buttonWidth = 25.0f;
            float rightWidth = 4 * buttonWidth + 5 * ImGui::GetStyle().ItemSpacing.x + 64.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth - 12.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.2f, 1.0f));
            if (ImGui::Button("Play", ImVec2(60.0f, 0))) actionPlay(window);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run the scene through its camera (F5)");
            const char* labels[] = {"W", "S", "M", "R"};
            const char* tips[] = {"Wireframe / heatmap (F1)", "Solid (F2)", "Material (F3)", "Rendered (F4)"};
            for (int i = 0; i < 4; i++) {
                if (toggle(labels[i], currentShadingMode == i, tips[i])) currentShadingMode = i;
            }
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    return height;
}

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

    bool editing = workspaceHasViewport(currentWorkspace);
    float top = drawMainMenu(window, editing);
    top += drawHeaderBar(window, top, editing);

    WorkArea area = { 0.0f, top, io.DisplaySize.x, std::max(io.DisplaySize.y - top, 1.0f) };
    // Без 3D-вида вьюпорт не занят — и SDF-кадр не считается
    if (!editing) viewportRect = vec4(0.0f);
    switch (currentWorkspace) {
        case WS_MODEL: drawModelWorkspace(window, area); break;
        case WS_SCENE: drawSceneWorkspace(window, area); break;
        case WS_UV: drawUvWorkspace(window, area); break;
        case WS_SCRIPT: drawScriptWorkspace(window, area); break;
        case WS_UI: drawUiWorkspace(window, area); break;
        default: break;
    }
    drawToast();

    drawExportWindow(window);
    drawConversionPopup();
    pollBlender();
    renderSettingsWindow();
    renderChangelogWindow();
    renderAboutWindow();
    ImGui::Render();
}

void setupImGuiStyle() {
    // Стиль собирается заново каждый кадр и масштабируется целиком — иначе масштаб накапливался бы
    ImGuiStyle style;
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
    style.ScaleAllSizes(editorCfg.uiScale);
    ImGui::GetStyle() = style;
}

