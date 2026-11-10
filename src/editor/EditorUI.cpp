#include "EditorUI.h"
#include "Common.h"
#include "Scene.h"
#include "Settings.h"
#include "Game.h"
#include "Renderer.h"
#include "EngineBridge.h"
#include "FileDialogs.h"
#include "MathUtils.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include <numeric>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

extern void* g_AppLogoID;
static bool showAboutWindow = false;
static bool isFirstRunCheckDone = false;
static int renamingNodeIdx = -1;

// Правки иерархии откладываются до конца обхода дерева, иначе ломаются итераторы
static int pendingReparentChild = -1, pendingReparentParent = -1;
static bool pendingDelete = false;

static const char* NOS_FILTER = "NOS Scene File (*.nos)\0*.nos\0All Files (*.*)\0*.*\0";
static const char* OBJ_FILTER = "OBJ Model (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";

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

void DrawSceneNode(int nodeIdx);

void TextCentered(std::string text) {
    float winWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.c_str());
}

// =================================================================
// --- Действия ---
// =================================================================
void actionSaveSceneAs(GLFWwindow* window) {
    std::string path = FileDialogs::SaveFile(window, NOS_FILTER, defaultExportPath);
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

void actionSelectAll() {
    selectedIndices.resize(sceneGraph.size());
    std::iota(selectedIndices.begin(), selectedIndices.end(), 0);
}

static void addMenuItems(GLFWwindow* window) {
    for (int t = 0; t < SDF_TYPE_COUNT; t++) {
        if (ImGui::MenuItem(shapeName(t))) createNode(t, shapeName(t));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Point Light")) createNode(NODE_LIGHT, "Light");
    if (ImGui::MenuItem("Empty")) createNode(NODE_EMPTY, "Empty");
    if (ImGui::MenuItem("Trigger Zone")) {
        int idx = createNode(SDF_BOX, "Trigger");
        if (idx >= 0) sceneGraph[idx].isTrigger = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Mesh (.obj)...")) {
        std::string path = FileDialogs::OpenFile(window, OBJ_FILTER);
        if (!path.empty()) loadMesh(path);
    }
}

// =================================================================
// --- Оверлеи вьюпорта: гизмо, рамки выделения, значки ---
// =================================================================
static bool drawSegment(ImDrawList* dl, vec3 a, vec3 b, ImU32 col, float thickness) {
    vec2 sa, sb;
    if (!projectWorldToScreen(a, sa) || !projectWorldToScreen(b, sb)) return false;
    dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, thickness);
    return true;
}

static void drawWireBox(ImDrawList* dl, const vec3 corners[8], ImU32 col, float thickness) {
    static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
    for (const auto& e : edges) drawSegment(dl, corners[e[0]], corners[e[1]], col, thickness);
}

// Углы ограничивающего бокса узла в мировых координатах
static bool nodeBoxCorners(int idx, vec3 corners[8]) {
    const SceneNode& node = sceneGraph[idx];
    if (node.meshIdx >= 0 && node.meshIdx < (int)meshCache.size()) {
        const MeshData& mesh = meshCache[node.meshIdx];
        mat4 worldMat = calculateWorldMatrix(idx);
        for (int i = 0; i < 8; i++) {
            vec3 p = { (i & 1) ? mesh.boundsMax.x : mesh.boundsMin.x, (i & 2) ? mesh.boundsMax.y : mesh.boundsMin.y, (i & 4) ? mesh.boundsMax.z : mesh.boundsMin.z };
            corners[i] = vec3(worldMat * vec4(p, 1.0f));
        }
        return true;
    }
    int type = nodeType(node);
    if (type < 0) return false;
    const ObjectData& od = node.gpuData;
    vec3 he = shapeHalfExtents(type, od.params.x, shapeParam2(type, od.rotationEuler.w)) * vec3(od.scale);
    mat3 rot = glm::transpose(mat3(od.invRotation));
    for (int i = 0; i < 8; i++) {
        vec3 offset = { (i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z };
        corners[i] = vec3(od.position) + rot * offset;
    }
    return true;
}

static void drawGizmo(ImDrawList* dl) {
    if (!showGizmos || selectedIndices.empty()) return;
    vec3 center = getSelectionCenter();
    vec2 sc;
    if (!projectWorldToScreen(center, sc)) return;
    CameraBasis b = getCameraBasis();
    float scale = glm::length(b.pos - center) * 0.12f;

    const vec3 axes[3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    const ImU32 colors[3] = { IM_COL32(230, 60, 60, 255), IM_COL32(60, 220, 60, 255), IM_COL32(70, 110, 240, 255) };
    const ImU32 hot = IM_COL32(255, 220, 60, 255);

    for (int i = 0; i < 3; i++) {
        bool active = isDraggingGizmo && currentGizmoAxis == AXIS_X + i;
        ImU32 col = active ? hot : colors[i];

        if (currentMode == MODE_ROTATE) {
            // Кольцо в плоскости, перпендикулярной оси
            vec3 u = axes[(i + 1) % 3], v = axes[(i + 2) % 3];
            const int segments = 48;
            ImVec2 pts[segments + 1];
            bool visible = true;
            for (int k = 0; k <= segments && visible; k++) {
                float a = (float)k / segments * 6.2831853f;
                vec2 s;
                visible = projectWorldToScreen(center + (u * std::cos(a) + v * std::sin(a)) * scale, s);
                pts[k] = ImVec2(s.x, s.y);
            }
            if (visible) dl->AddPolyline(pts, segments + 1, col, 0, active ? 3.5f : 2.0f);
            continue;
        }

        vec2 st;
        if (!projectWorldToScreen(center + axes[i] * (1.3f * scale), st)) continue;
        if (active && currentMode == MODE_MOVE) drawSegment(dl, center - axes[i] * (scale * 40.0f), center + axes[i] * (scale * 40.0f), (col & 0x00FFFFFF) | 0x90000000, 1.0f);
        dl->AddLine(ImVec2(sc.x, sc.y), ImVec2(st.x, st.y), col, 2.5f);
        vec2 dir = st - sc;
        float len = glm::length(dir);
        if (len < 4.0f) continue;
        dir /= len;
        if (currentMode == MODE_MOVE) {
            vec2 base = st - dir * 14.0f, perp = vec2(-dir.y, dir.x) * 6.0f;
            dl->AddTriangleFilled(ImVec2(st.x + dir.x * 2.0f, st.y + dir.y * 2.0f), ImVec2(base.x + perp.x, base.y + perp.y), ImVec2(base.x - perp.x, base.y - perp.y), col);
        } else {
            dl->AddRectFilled(ImVec2(st.x - 5.0f, st.y - 5.0f), ImVec2(st.x + 5.0f, st.y + 5.0f), col);
        }
    }
    if (currentMode != MODE_ROTATE) {
        bool active = isDraggingGizmo && currentGizmoAxis == AXIS_CENTER;
        dl->AddCircleFilled(ImVec2(sc.x, sc.y), 6.0f, active ? hot : IM_COL32(255, 255, 255, 255));
    }
}

static void drawViewportOverlays() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 vpMin(viewportRect.x, viewportRect.y), vpMax(viewportRect.x + viewportRect.z, viewportRect.y + viewportRect.w);
    dl->PushClipRect(vpMin, vpMax, true);

    // Значки узлов без собственной геометрии
    for (int i = 0; i < (int)sceneGraph.size(); i++) {
        const SceneNode& node = sceneGraph[i];
        if (!node.worldActive) continue;
        int type = nodeType(node);
        vec2 s;
        if (node.isTrigger) {
            vec3 corners[8];
            if (nodeBoxCorners(i, corners)) drawWireBox(dl, corners, IM_COL32(80, 230, 120, 200), 1.0f);
        } else if (type == NODE_LIGHT && projectWorldToScreen(vec3(node.gpuData.position), s)) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(node.gpuData.color.x, node.gpuData.color.y, node.gpuData.color.z, 1.0f));
            dl->AddCircleFilled(ImVec2(s.x, s.y), 5.0f, col);
            for (int k = 0; k < 8; k++) {
                float a = k * 0.7853982f;
                dl->AddLine(ImVec2(s.x + std::cos(a) * 8.0f, s.y + std::sin(a) * 8.0f), ImVec2(s.x + std::cos(a) * 13.0f, s.y + std::sin(a) * 13.0f), col, 1.5f);
            }
        } else if (type == NODE_EMPTY && node.meshIdx < 0 && projectWorldToScreen(vec3(node.gpuData.position), s)) {
            dl->AddLine(ImVec2(s.x - 6, s.y), ImVec2(s.x + 6, s.y), IM_COL32(200, 200, 200, 200), 1.5f);
            dl->AddLine(ImVec2(s.x, s.y - 6), ImVec2(s.x, s.y + 6), IM_COL32(200, 200, 200, 200), 1.5f);
        }
    }

    // Рамки выделения
    for (int idx : selectedIndices) {
        if (idx < 0 || idx >= (int)sceneGraph.size() || !sceneGraph[idx].worldActive) continue;
        vec3 corners[8];
        vec2 s;
        if (nodeBoxCorners(idx, corners)) drawWireBox(dl, corners, IM_COL32(255, 140, 0, 200), 1.5f);
        else if (projectWorldToScreen(vec3(sceneGraph[idx].gpuData.position), s)) dl->AddCircle(ImVec2(s.x, s.y), 16.0f, IM_COL32(255, 140, 0, 220), 24, 2.0f);
    }

    drawGizmo(dl);

    if (isBoxSelecting) {
        ImVec2 p1(std::min((float)mousePressX, (float)currentMouseX), std::min((float)mousePressY, (float)currentMouseY));
        ImVec2 p2(std::max((float)mousePressX, (float)currentMouseX), std::max((float)mousePressY, (float)currentMouseY));
        dl->AddRect(p1, p2, IM_COL32(255, 255, 255, 150), 0.0f, 0, 1.0f);
        dl->AddRectFilled(p1, p2, IM_COL32(255, 255, 255, 30));
    }

    if (editorCfg.showStats) {
        char buf[256];
        float scalePct = renderStats.viewH > 0 ? 100.0f * renderStats.renderH / renderStats.viewH : 0.0f;
        snprintf(buf, sizeof(buf), "%.0f FPS | SDF %dx%d (%.0f%%) | GPU %.2f ms | %d objects, %d lights%s",
                 ImGui::GetIO().Framerate, renderStats.renderW, renderStats.renderH, scalePct, renderStats.gpuMs,
                 (int)gpuObjectCache.size(), (int)gpuLightCache.size(), renderStats.renderedThisFrame ? "" : " | idle");
        dl->AddText(ImVec2(vpMin.x + 9, vpMin.y + 7), IM_COL32(0, 0, 0, 200), buf);
        dl->AddText(ImVec2(vpMin.x + 8, vpMin.y + 6), IM_COL32(230, 230, 230, 255), buf);
    }
    dl->PopClipRect();
}

// =================================================================
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
                char dirBuf[512];
                strncpy(dirBuf, editorCfg.assetDir.c_str(), sizeof(dirBuf)); dirBuf[sizeof(dirBuf) - 1] = 0;
                if (ImGui::InputText("Asset folder", dirBuf, sizeof(dirBuf))) { editorCfg.assetDir = dirBuf; changed = true; }
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

// =================================================================
// --- Браузер ассетов ---
// =================================================================
struct AssetEntry {
    std::string path;
    std::string name;
    void* texture = nullptr;
};
static std::vector<AssetEntry> assetEntries;
static bool assetsScanned = false;
static std::string assetsScannedDir;

static void scanAssets() {
    for (auto& e : assetEntries) if (e.texture) destroyUiTexture(e.texture);
    assetEntries.clear();
    assetsScanned = true;
    assetsScannedDir = editorCfg.assetDir;

    std::error_code ec;
    if (!std::filesystem::is_directory(editorCfg.assetDir, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(editorCfg.assetDir, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".nos") continue;
        AssetEntry e;
        e.path = entry.path().string();
        e.name = entry.path().stem().string();
        assetEntries.push_back(e);
        if (assetEntries.size() >= 64) break;
    }
    std::sort(assetEntries.begin(), assetEntries.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.name < b.name; });
    for (auto& e : assetEntries) {
        std::vector<uint8_t> pixels;
        if (readNosThumbnail(e.path, pixels)) e.texture = createUiTexture(pixels.data(), NOS_THUMB_W, NOS_THUMB_H);
    }
}

static void renderAssetBrowser(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Assets", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    // Пересканирование — до первого ImGui::Image, пока старые текстуры ещё не попали в кадр
    bool rescan = !assetsScanned || assetsScannedDir != editorCfg.assetDir;

    ImGui::TextDisabled("ASSETS");
    ImGui::SameLine(); ImGui::TextDisabled("(%s)", editorCfg.assetDir.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) rescan = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Save scene as asset")) {
        std::error_code ec;
        std::filesystem::create_directories(editorCfg.assetDir, ec);
        std::string base = (currentLoadedFile == "Untitled") ? "asset" : std::filesystem::path(currentLoadedFile).stem().string();
        std::string path;
        for (int i = 0; i < 1000; i++) {
            path = editorCfg.assetDir + "/" + base + (i ? "_" + std::to_string(i) : "") + ".nos";
            if (!std::filesystem::exists(path, ec)) break;
        }
        std::string previous = currentLoadedFile;
        saveNos(path);
        currentLoadedFile = previous;   // Это копия в библиотеку, а не «Сохранить как»
        rescan = true;
    }
    if (rescan) scanAssets();
    ImGui::Separator();

    ImGui::BeginChild("AssetGrid", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    if (assetEntries.empty()) ImGui::TextDisabled("No .nos files here yet. Double-click an asset to open it, drag it into the viewport to append.");
    const ImVec2 thumb(128, 72);
    for (int i = 0; i < (int)assetEntries.size(); i++) {
        const AssetEntry& e = assetEntries[i];
        ImGui::PushID(i);
        ImGui::BeginGroup();
        if (e.texture) ImGui::ImageButton("##thumb", (ImTextureID)e.texture, thumb);
        else ImGui::Button("No preview", ImVec2(thumb.x + 8, thumb.y + 8));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) loadNos(e.path);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ASSET_NOS", &i, sizeof(int));
            ImGui::Text("Append %s", e.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Open")) loadNos(e.path);
            if (ImGui::MenuItem("Append to scene")) appendNos(e.path);
            ImGui::EndPopup();
        }
        ImGui::TextUnformatted(e.name.substr(0, 18).c_str());
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::EndChild();
    ImGui::End();

    // Ассет бросили во вьюпорт
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType("ASSET_NOS") && ImGui::IsMouseReleased(0)) {
        ImVec2 m = ImGui::GetMousePos();
        int idx = *(const int*)payload->Data;
        bool inViewport = m.x >= viewportRect.x && m.y >= viewportRect.y && m.x < viewportRect.x + viewportRect.z && m.y < viewportRect.y + viewportRect.w;
        if (inViewport && idx >= 0 && idx < (int)assetEntries.size()) appendNos(assetEntries[idx].path);
    }
}

// =================================================================
// --- Инспектор ---
// =================================================================
static void undoOnEdit() { if (ImGui::IsItemActivated()) saveUndo(); }

static void renderInspector() {
    if (selectedIndices.empty()) { ImGui::TextDisabled("Select an object in hierarchy"); return; }
    int activeIdx = selectedIndices.back();
    if (activeIdx < 0 || activeIdx >= (int)sceneGraph.size()) return;
    SceneNode& node = sceneGraph[activeIdx];
    int type = nodeType(node);
    bool isMesh = node.meshIdx >= 0;
    bool isSdf = isSdfNode(node);

    char nameBuf[128];
    strncpy(nameBuf, node.name.c_str(), sizeof(nameBuf)); nameBuf[sizeof(nameBuf) - 1] = 0;
    ImGui::PushItemWidth(-70);
    if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) node.name = nameBuf;
    undoOnEdit();
    ImGui::PopItemWidth();
    ImGui::SameLine();
    bool active = node.active;
    if (ImGui::Checkbox("Active", &active)) { saveUndo(); node.active = active; }
    ImGui::Spacing();
    ImGui::Separator();

    if (isSdf) {
        if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginCombo("Shape", shapeName(type))) {
                for (int t = 0; t < SDF_TYPE_COUNT; t++) {
                    if (ImGui::Selectable(shapeName(t), t == type)) { saveUndo(); node.gpuData.position.w = (float)t; node.gpuData.rotationEuler.w = 0.0f; }
                }
                ImGui::EndCombo();
            }
            const char* ops[] = { "Union", "Subtract", "Intersect" };
            int op = std::clamp((int)std::lround(node.gpuData.params.y), 0, 2);
            if (ImGui::Combo("Operation", &op, ops, 3)) { saveUndo(); node.gpuData.params.y = (float)op; }
            ImGui::DragFloat("Radius/Size", &node.gpuData.params.x, 0.05f, 0.01f, 100.0f); undoOnEdit();
            if (type == SDF_TORUS || type == SDF_CAPSULE || type == SDF_CONE) {
                const char* label = (type == SDF_TORUS) ? "Thickness" : (type == SDF_CAPSULE) ? "Radius ratio" : "Top radius";
                float p2 = shapeParam2(type, node.gpuData.rotationEuler.w);
                // 0 в файле означает «значение по умолчанию», поэтому у тора и капсулы минимум чуть выше нуля
                float minV = (type == SDF_CONE) ? 0.0f : 0.02f;
                if (ImGui::SliderFloat(label, &p2, minV, 1.0f)) node.gpuData.rotationEuler.w = p2;
                undoOnEdit();
            }
            ImGui::DragFloat("Smoothness", &node.gpuData.params.z, 0.01f, 0.0f, 2.0f); undoOnEdit();
        }
    } else if (type == NODE_LIGHT) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &node.gpuData.color.x); undoOnEdit();
            ImGui::DragFloat("Intensity", &node.lightIntensity, 0.1f, 0.0f, 200.0f); undoOnEdit();
            ImGui::DragFloat("Range", &node.lightRange, 0.1f, 0.1f, 200.0f); undoOnEdit();
            if ((int)gpuLightCache.size() >= MAX_LIGHTS) ImGui::TextDisabled("Only the first %d lights are rendered", MAX_LIGHTS);
        }
    } else if (isMesh) {
        if (ImGui::CollapsingHeader("Mesh Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Mesh ID: %d", node.meshIdx);
            bool loaded = node.meshIdx < (int)meshCache.size() && meshCache[node.meshIdx].isLoaded;
            ImGui::Text("File: %s", loaded ? "Loaded" : "Failed");
            if (loaded) ImGui::Text("Triangles: %u", meshCache[node.meshIdx].indexBuffer.count / 3);
        }
    }

    if ((isSdf && !node.isTrigger) || isMesh) {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &node.gpuData.color.x); undoOnEdit();
            if (isSdf) {
                ImGui::SliderFloat("Metallic", &node.gpuData.params.w, 0.0f, 1.0f); undoOnEdit();
                ImGui::SliderFloat("Roughness", &node.gpuData.color.w, 0.0f, 1.0f); undoOnEdit();
                if (ImGui::BeginCombo("Preset", "Choose...")) {
                    for (const auto& preset : materialPresets) {
                        if (ImGui::Selectable(preset.name)) {
                            saveUndo();
                            // Пресет применяется ко всем выделенным объектам
                            for (int idx : selectedIndices) if (idx < (int)sceneGraph.size() && isSdfNode(sceneGraph[idx])) applyMaterialPreset(sceneGraph[idx], preset);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Pos", &node.localPosition.x, 0.05f); undoOnEdit();
        vec3 deg = glm::degrees(node.localRotation);
        if (ImGui::DragFloat3("Rot", &deg.x, 0.5f, 0.0f, 0.0f, "%.1f")) node.localRotation = glm::radians(deg);
        undoOnEdit();
        ImGui::DragFloat3("Scale", &node.localScale.x, 0.05f); undoOnEdit();
        // Нулевой масштаб ломает SDF — не даём до него дотянуть
        for (int k = 0; k < 3; k++) if (std::abs(node.localScale[k]) < 0.001f) node.localScale[k] = 0.001f;
    }

    if (isSdf && !node.isTrigger && ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* bodyTypes[] = { "Static (world geometry)", "Dynamic" };
        int bodyType = node.body.type;
        if (ImGui::Combo("Body", &bodyType, bodyTypes, BODY_TYPE_COUNT)) { saveUndo(); node.body.type = bodyType; }
        if (node.body.type == BODY_DYNAMIC) {
            ImGui::DragFloat("Mass", &node.body.mass, 0.05f, 0.01f, 1000.0f, "%.2f kg"); undoOnEdit();
            ImGui::SliderFloat("Bounciness", &node.body.restitution, 0.0f, 1.0f); undoOnEdit();
            ImGui::SliderFloat("Friction", &node.body.friction, 0.0f, 1.5f); undoOnEdit();
            ImGui::DragFloat("Linear damping", &node.body.linearDamping, 0.01f, 0.0f, 10.0f); undoOnEdit();
            ImGui::DragFloat("Angular damping", &node.body.angularDamping, 0.01f, 0.0f, 10.0f); undoOnEdit();
            ImGui::DragFloat("Gravity scale", &node.body.gravityScale, 0.05f, -5.0f, 5.0f); undoOnEdit();
            ImGui::TextDisabled("Collider: this node's shape. Simulated in Play Mode.");
        }
    }

    if (ImGui::CollapsingHeader("Logic (Play Mode)")) {
        const char* behaviors[] = { "None", "Rotate", "Ping-Pong" };
        int behavior = node.behavior;
        if (ImGui::Combo("Behavior", &behavior, behaviors, BEHAVIOR_COUNT)) { saveUndo(); node.behavior = behavior; }
        if (node.behavior != BEHAVIOR_NONE) {
            ImGui::DragFloat3(node.behavior == BEHAVIOR_ROTATE ? "Axis speed" : "Offset", &node.behaviorVec.x, 0.05f); undoOnEdit();
            ImGui::DragFloat("Speed", &node.behaviorSpeed, 0.05f); undoOnEdit();
            bool on = node.behaviorOn;
            if (ImGui::Checkbox("Running at start", &on)) { saveUndo(); node.behaviorOn = on; }
        }
        if (isSdf) {
            bool trigger = node.isTrigger;
            if (ImGui::Checkbox("Trigger zone (invisible box)", &trigger)) { saveUndo(); node.isTrigger = trigger; }
        }
        if (node.isTrigger) {
            int target = node.triggerTarget;
            const char* preview = (target >= 0 && target < (int)sceneGraph.size()) ? sceneGraph[target].name.c_str() : "(none)";
            if (ImGui::BeginCombo("Target", preview)) {
                if (ImGui::Selectable("(none)", target == -1)) { saveUndo(); node.triggerTarget = -1; }
                for (int i = 0; i < (int)sceneGraph.size(); i++) {
                    if (i == activeIdx) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(sceneGraph[i].name.c_str(), target == i)) { saveUndo(); node.triggerTarget = i; }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            const char* actions[] = { "Toggle visibility", "Start behavior", "Stop behavior", "Toggle behavior" };
            int action = node.triggerAction;
            if (ImGui::Combo("On enter", &action, actions, TRIGGER_ACTION_COUNT)) { saveUndo(); node.triggerAction = action; }
        }
    }
}

// =================================================================
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

    float menuHeight = 0.0f;
    bool openAddPopup = false;
    if (ImGui::BeginMainMenuBar()) {
        menuHeight = ImGui::GetWindowSize().y;

        const char* modes[] = { "Object Editor", "Map Editor", "Game Build Editor" };
        static int currentModeIdx = 0;
        ImGui::PushItemWidth(150);
        if (ImGui::Combo("##Mode", &currentModeIdx, modes, IM_ARRAYSIZE(modes))) {
            if (currentModeIdx == 0) currentEditorMode = EDITOR_OBJECT;
            if (currentModeIdx == 1) currentEditorMode = EDITOR_MAP;
            if (currentModeIdx == 2) currentEditorMode = EDITOR_GAME_BUILD;
        }
        ImGui::PopItemWidth();
        ImGui::Separator();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) { resetScene(); }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) actionOpenScene(window);
            if (ImGui::MenuItem("Append...")) { std::string path = FileDialogs::OpenFile(window, NOS_FILTER); if (!path.empty()) appendNos(path); }
            if (ImGui::MenuItem("Save", "Ctrl+S")) actionSaveScene(window);
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) actionSaveSceneAs(window);
            ImGui::Separator();
            if (ImGui::MenuItem("Import Mesh (.obj)...")) { std::string path = FileDialogs::OpenFile(window, OBJ_FILTER); if (!path.empty()) loadMesh(path); }
            if (ImGui::MenuItem("Export Game...")) {
                std::string dir = FileDialogs::SelectFolder(window);
                if (!dir.empty()) showToast(exportGame(dir) ? "Game exported: " + dir : "Game export failed");
            }
            ImGui::Separator();
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
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.2f, 1.0f));
        if (ImGui::Button("Play", ImVec2(60.0f, 0))) startPlay(window);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Mode (F5)");
        ImGui::SameLine();
        const char* labels[] = {"W", "S", "M", "R"};
        const char* tips[] = {"Wireframe / heatmap (F1)", "Solid (F2)", "Material (F3)", "Rendered (F4)"};
        for(int i=0; i<4; i++) {
            bool current = currentShadingMode == i;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.90f, 1.0f));
            if (ImGui::Button(labels[i], ImVec2(buttonWidth, 0))) currentShadingMode = i;
            if (current) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
            if(i < 3) ImGui::SameLine();
        }
        ImGui::EndMainMenuBar();
    }

    // --- Раскладка: панели вплотную к краям, всё остальное — вьюпорт ---
    float toolbarWidth = showToolbar ? 50.0f : 0.0f;
    float rightPanelWidth = showSidebar ? std::min(320.0f, io.DisplaySize.x * 0.45f) : 0.0f;
    float topOffset = menuHeight;
    float workspaceHeight = std::max(io.DisplaySize.y - topOffset, 1.0f);
    float assetHeight = showAssetBrowser ? std::min(150.0f, workspaceHeight * 0.4f) : 0.0f;
    float centerWidth = std::max(io.DisplaySize.x - toolbarWidth - rightPanelWidth, 1.0f);
    viewportRect = vec4(toolbarWidth, topOffset, centerWidth, std::max(workspaceHeight - assetHeight, 1.0f));

    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (showToolbar) {
        ImGui::SetNextWindowPos(ImVec2(0, topOffset)); ImGui::SetNextWindowSize(ImVec2(toolbarWidth, workspaceHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, guiAlpha));
        ImGui::Begin("Tools", nullptr, panelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        {
            auto toolButton = [](const char* label, bool on, const char* tip) {
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.90f, 1.0f));
                bool clicked = ImGui::Button(label, ImVec2(30, 30));
                if (on) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                return clicked;
            };
            if (toolButton("Mov", currentMode == MODE_MOVE, "Move (1)")) currentMode = MODE_MOVE;
            if (toolButton("Rot", currentMode == MODE_ROTATE, "Rotate (2)")) currentMode = MODE_ROTATE;
            if (toolButton("Scl", currentMode == MODE_SCALE, "Scale (3)")) currentMode = MODE_SCALE;
            ImGui::Separator();
            if (toolButton("Giz", showGizmos, "Show gizmos")) showGizmos = !showGizmos;
            if (toolButton("Snp", editorCfg.snap, "Snap to grid (hold Ctrl to invert)")) { editorCfg.snap = !editorCfg.snap; saveSettings(settingsPath()); }
            if (toolButton("Grd", editorCfg.showGrid, "Floor grid (G)")) { editorCfg.showGrid = !editorCfg.showGrid; saveSettings(settingsPath()); }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    if (showSidebar) {
        float rightX = io.DisplaySize.x - rightPanelWidth;
        float hierarchyHeight = workspaceHeight * 0.4f;
        float inspectorHeight = workspaceHeight - hierarchyHeight;
        ImGui::SetNextWindowPos(ImVec2(rightX, topOffset)); ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, hierarchyHeight));
        ImGui::Begin("Scene Collection", nullptr, panelFlags);
        {
            ImGui::TextDisabled("HIERARCHY");
            ImGui::SameLine(); ImGui::TextDisabled("(%d / %d)", (int)sceneGraph.size(), MAX_OBJECTS);
            ImGui::Separator();
            ImGui::BeginChild("HierarchyScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

            // Рендер корневых узлов
            for (int i = 0; i < (int)sceneGraph.size(); i++) {
                if (sceneGraph[i].parent == -1) DrawSceneNode(i);
            }

            // Зона для сброса в корень (Drop to Unparent)
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 30.0f));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE")) {
                    pendingReparentChild = *(const int*)payload->Data;
                    pendingReparentParent = -1; // Сброс родителя (стать сиротой)
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 30.0f);
            ImGui::TextDisabled("(Drop here to unparent)");

            ImGui::EndChild();
        }
        ImGui::End();

        if (pendingReparentChild >= 0) {
            saveUndo();
            reparent(pendingReparentChild, pendingReparentParent);
            pendingReparentChild = -1;
        }
        if (pendingDelete) { deleteSelectedObject(); pendingDelete = false; }

        ImGui::SetNextWindowPos(ImVec2(rightX, topOffset + hierarchyHeight)); ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorHeight));
        ImGui::Begin("Inspector", nullptr, panelFlags);
        renderInspector();
        ImGui::End();
    }

    if (showAssetBrowser) renderAssetBrowser(toolbarWidth, topOffset + workspaceHeight - assetHeight, centerWidth, assetHeight);

    drawViewportOverlays();
    drawToast();

    if (triggerSceneContextMenu) { ImGui::OpenPopup("SceneContextMenu"); triggerSceneContextMenu = false; }
    if (ImGui::BeginPopup("SceneContextMenu")) {
        if (ImGui::MenuItem("Duplicate")) { duplicateSelectedObject(); }
        if (ImGui::MenuItem("Delete")) { deleteSelectedObject(); }
        if (ImGui::BeginMenu("Add")) { addMenuItems(window); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }

    renderSettingsWindow();
    renderChangelogWindow();
    renderAboutWindow();
    ImGui::Render();
}

void DrawSceneNode(int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= (int)sceneGraph.size()) return;
    SceneNode& node = sceneGraph[nodeIdx];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

    bool isSelected = std::find(selectedIndices.begin(), selectedIndices.end(), nodeIdx) != selectedIndices.end();
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    if (renamingNodeIdx == nodeIdx) {
        char buf[128];
        strncpy(buf, node.name.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            saveUndo();
            node.name = buf;
            renamingNodeIdx = -1;
        }
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
            renamingNodeIdx = -1;
        }
    } else {
        // Скрытые узлы показываем приглушённо
        if (!node.worldActive) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const char* label = node.name.empty() ? "(unnamed)" : node.name.c_str();
        bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)nodeIdx, flags, "%s", label);
        if (!node.worldActive) ImGui::PopStyleColor();

        // --- DRAG AND DROP SOURCE ---
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("SCENE_NODE", &nodeIdx, sizeof(int));
            ImGui::Text("Move %s", label);
            ImGui::EndDragDropSource();
        }

        // --- DRAG AND DROP TARGET ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE")) {
                pendingReparentChild = *(const int*)payload->Data;
                pendingReparentParent = nodeIdx;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            renamingNodeIdx = nodeIdx;
        }

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            if (!ImGui::GetIO().KeyShift) selectedIndices.clear();
            if (std::find(selectedIndices.begin(), selectedIndices.end(), nodeIdx) == selectedIndices.end()) selectedIndices.push_back(nodeIdx);
        }

        if (ImGui::BeginPopupContextItem()) {
            // Действия из контекстного меню относятся к узлу под курсором
            if (!isSelected) selectedIndices = { nodeIdx };
            if (ImGui::MenuItem("Rename")) { renamingNodeIdx = nodeIdx; }
            if (ImGui::MenuItem(node.active ? "Hide" : "Show")) { saveUndo(); node.active = !node.active; }
            if (ImGui::MenuItem("Duplicate")) { duplicateSelectedObject(); ImGui::EndPopup(); if (nodeOpen) ImGui::TreePop(); return; }
            if (ImGui::MenuItem("Delete")) { pendingDelete = true; }
            ImGui::EndPopup();
        }

        if (nodeOpen) {
            // Копия списка: действия выше могли изменить граф
            std::vector<int> children = sceneGraph[nodeIdx].children;
            for (int childIdx : children) DrawSceneNode(childIdx);
            ImGui::TreePop();
        }
    }
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
