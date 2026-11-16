#include "Panels.h"
#include "EditorUI.h"
#include "Scene.h"
#include "Settings.h"
#include "Game.h"
#include "Renderer.h"
#include "Project.h"
#include "Conversion.h"
#include "BlenderLink.h"
#include "ScriptSystem.h"
#include "EngineBridge.h"
#include "FileDialogs.h"
#include "MathUtils.h"
#include "Input.h"
#include "imgui.h"
#include <numeric>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

static int renamingNodeIdx = -1;

// Правки иерархии откладываются до конца обхода дерева, иначе ломаются итераторы
static int pendingReparentChild = -1, pendingReparentParent = -1;
static bool pendingDelete = false;

const char* NOS_FILTER = "NOS Scene File (*.nos)\0*.nos\0All Files (*.*)\0*.*\0";
const char* OBJ_FILTER = "OBJ Model (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
const char* SCRIPT_FILTER = "NLS Script (*.nls)\0*.nls\0All Files (*.*)\0*.*\0";
const char* SOUND_FILTER = "Sound (*.wav *.mp3 *.flac)\0*.wav;*.mp3;*.flac\0All Files (*.*)\0*.*\0";

static const ImGuiWindowFlags PANEL_FLAGS = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

static void DrawSceneNode(int nodeIdx);

void TextCentered(std::string text) {
    float winWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.c_str());
}


void addMenuItems(GLFWwindow* window) {
    for (int t = 0; t < SDF_PRIMITIVE_COUNT; t++) {
        if (ImGui::MenuItem(shapeName(t))) createNode(t, shapeName(t));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Camera")) createNode(NODE_CAMERA, "Camera");
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

void drawViewportOverlays() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 vpMin(viewportRect.x, viewportRect.y), vpMax(viewportRect.x + viewportRect.z, viewportRect.y + viewportRect.w);
    dl->PushClipRect(vpMin, vpMax, true);
    drawEditorParticlePreview(dl);

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
        } else if (type == NODE_CAMERA) {
            // Пирамида обзора: куда и под каким углом смотрит камера
            mat3 rot = glm::transpose(mat3(node.gpuData.invRotation));
            vec3 pos = vec3(node.gpuData.position);
            float h = std::tan(glm::radians(node.cameraFov) * 0.5f) * 0.8f, w = h * 16.0f / 9.0f;
            vec3 corners[4];
            for (int k = 0; k < 4; k++) corners[k] = pos + rot * vec3((k & 1) ? w : -w, (k & 2) ? h : -h, -0.8f);
            const ImU32 col = IM_COL32(120, 200, 255, 230);
            for (int k = 0; k < 4; k++) drawSegment(dl, pos, corners[k], col, 1.5f);
            drawSegment(dl, corners[0], corners[1], col, 1.5f); drawSegment(dl, corners[1], corners[3], col, 1.5f);
            drawSegment(dl, corners[3], corners[2], col, 1.5f); drawSegment(dl, corners[2], corners[0], col, 1.5f);
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


// --- Браузер ассетов ---
// =================================================================
enum AssetKind { ASSET_MODEL, ASSET_PREFAB, ASSET_SOUND, ASSET_SCRIPT };
struct AssetEntry {
    std::string path;
    std::string name;
    void* texture = nullptr;
    int kind = ASSET_MODEL;
};
static std::vector<AssetEntry> assetEntries;
static bool assetsScanned = false;
static std::string assetsScannedDir;

static std::string assetDirectory() { return project.path("assets"); }

void rescanAssets() { assetsScanned = false; }

static void scanAssets() {
    for (auto& e : assetEntries) if (e.texture) destroyUiTexture(e.texture);
    assetEntries.clear();
    assetsScanned = true;
    assetsScannedDir = assetDirectory();

    std::error_code ec;
    // Модели, префабы, звуки и скрипты проекта: всё, что можно бросить во вьюпорт
    auto collect = [&](const std::string& dir, int kind, std::initializer_list<const char*> extensions) {
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || assetEntries.size() >= 256) continue;
            std::string ext = entry.path().extension().string();
            if (std::find_if(extensions.begin(), extensions.end(), [&](const char* x) { return ext == x; }) == extensions.end()) continue;
            AssetEntry e;
            e.path = entry.path().string();
            e.name = kind == ASSET_MODEL || kind == ASSET_PREFAB ? entry.path().stem().string() : entry.path().filename().string();
            e.kind = kind;
            assetEntries.push_back(e);
        }
    };
    collect(assetDirectory(), ASSET_MODEL, { ".nos" });
    collect(project.path("assets/prefabs"), ASSET_PREFAB, { ".nos" });
    collect(project.path("assets/sounds"), ASSET_SOUND, { ".wav", ".mp3", ".flac", ".ogg" });
    collect(project.path("scripts"), ASSET_SCRIPT, { ".nls", ".nlsg" });
    std::stable_sort(assetEntries.begin(), assetEntries.end(), [](const AssetEntry& a, const AssetEntry& b) { return a.kind != b.kind ? a.kind < b.kind : a.name < b.name; });
    for (auto& e : assetEntries) {
        std::vector<uint8_t> pixels;
        if (e.kind <= ASSET_PREFAB && readNosThumbnail(e.path, pixels)) e.texture = createUiTexture(pixels.data(), NOS_THUMB_W, NOS_THUMB_H);
    }
}

// Точка сцены под курсором: куда встанет брошенный ассет. false — луч ушёл в небо.
static bool pointUnderMouse(ImVec2 mouse, vec3& point) {
    Ray ray = getRayFromMouse(mouse.x, mouse.y);
    float t = 0.0f;
    for (int step = 0; step < 128 && t < 200.0f; step++) {
        float d = sceneDistance(ray.origin + ray.dir * t);
        if (d < 0.002f) { point = ray.origin + ray.dir * t; return true; }
        t += d;
    }
    return false;
}

// Ассет отпустили над вьюпортом
static void dropAsset(const AssetEntry& e, ImVec2 mouse) {
    if (e.kind == ASSET_MODEL || e.kind == ASSET_PREFAB) {
        vec3 point;
        bool hit = pointUnderMouse(mouse, point);
        int before = (int)sceneGraph.size();
        appendNos(e.path);
        // Добавленное встаёт туда, куда показали мышью (корни сдвигаются, дети едут следом)
        if (hit) for (int i = before; i < (int)sceneGraph.size(); i++) if (sceneGraph[i].parent < before) sceneGraph[i].localPosition += point;
        return;
    }
    int target = pickNode(getRayFromMouse(mouse.x, mouse.y));
    if (target < 0) { showToast("Drop it on an object"); return; }
    saveUndo();
    SceneNode& node = sceneGraph[target];
    std::string rel = project.relative(e.path);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    if (e.kind == ASSET_SOUND) { node.audio.enabled = true; node.audio.clip = rel; showToast("Sound on " + node.name + ": " + e.name); }
    else { node.script.path = rel; node.script.vars.clear(); showToast("Script on " + node.name + ": " + e.name); }
    selectedIndices = { target };
}

static void assetBrowserContent() {
    // Пересканирование — до первого ImGui::Image, пока старые текстуры ещё не попали в кадр
    bool rescan = !assetsScanned || assetsScannedDir != assetDirectory();

    ImGui::TextDisabled("ASSETS");
    ImGui::SameLine(); ImGui::TextDisabled("(%s)", project.relative(assetDirectory()).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) rescan = true;
    ImGui::SameLine();
    if (ImGui::SmallButton(editingModel() ? "Save model to assets" : "Save scene as asset")) {
        std::error_code ec;
        std::filesystem::create_directories(assetDirectory(), ec);
        std::string base = (currentLoadedFile == "Untitled") ? "asset" : std::filesystem::path(currentLoadedFile).stem().string();
        std::string path;
        for (int i = 0; i < 1000; i++) {
            path = assetDirectory() + "/" + base + (i ? "_" + std::to_string(i) : "") + ".nos";
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
    if (assetEntries.empty()) ImGui::TextDisabled("Nothing here yet. Drag models and prefabs into the viewport; sounds and scripts — onto an object.");
    static const char* kindLabels[] = { "model", "prefab", "sound", "script" };
    const ImVec2 thumb(128, 72);
    for (int i = 0; i < (int)assetEntries.size(); i++) {
        const AssetEntry& e = assetEntries[i];
        ImGui::PushID(i);
        ImGui::BeginGroup();
        if (e.texture) ImGui::ImageButton("##thumb", (ImTextureID)e.texture, thumb);
        else ImGui::Button(kindLabels[e.kind], ImVec2(thumb.x + 8, thumb.y + 8));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (e.kind <= ASSET_PREFAB) loadNos(e.path);
            else if (e.kind == ASSET_SCRIPT) { openScriptInEditor(project.relative(e.path)); }
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("ASSET_NOS", &i, sizeof(int));
            ImGui::Text(e.kind <= ASSET_PREFAB ? "Place %s" : "Put %s on an object", e.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (e.kind <= ASSET_PREFAB) {
                if (ImGui::MenuItem("Open")) loadNos(e.path);
                if (ImGui::MenuItem("Append to scene")) appendNos(e.path);
            } else if (e.kind == ASSET_SCRIPT) {
                if (ImGui::MenuItem("Edit")) openScriptInEditor(project.relative(e.path));
            } else ImGui::TextDisabled("Drag onto an object");
            ImGui::EndPopup();
        }
        ImGui::TextUnformatted(e.name.substr(0, 18).c_str());
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::EndChild();

    // Ассет бросили во вьюпорт
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType("ASSET_NOS") && ImGui::IsMouseReleased(0)) {
        ImVec2 m = ImGui::GetMousePos();
        int idx = *(const int*)payload->Data;
        bool inViewport = m.x >= viewportRect.x && m.y >= viewportRect.y && m.x < viewportRect.x + viewportRect.z && m.y < viewportRect.y + viewportRect.w;
        if (inViewport && idx >= 0 && idx < (int)assetEntries.size()) dropAsset(assetEntries[idx], m);
    }
}

// Вывод скриптов: то же, что вкладка Console в Script Editor
static void consoleContent() {
    if (ImGui::SmallButton("Clear")) nls::scripts.console.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("log() output and script errors");
    ImGui::BeginChild("BottomConsole");
    for (const auto& line : nls::scripts.console) {
        if (line.compare(0, 5, "ERROR") == 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
        else ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// Нижняя панель: ассеты, таймлайн и консоль вкладками
void drawAssetBrowser(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Assets", nullptr, PANEL_FLAGS | ImGuiWindowFlags_NoTitleBar);
    static size_t seenLines = 0;
    bool newErrors = false;
    for (size_t i = seenLines; i < nls::scripts.console.size(); i++) if (nls::scripts.console[i].compare(0, 5, "ERROR") == 0) newErrors = true;
    if (nls::scripts.console.size() < seenLines) seenLines = 0;
    if (ImGui::BeginTabBar("BottomTabs")) {
        if (ImGui::BeginTabItem("Assets")) { assetBrowserContent(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Timeline")) { drawTimelinePanel(); ImGui::EndTabItem(); }
        // Непрочитанные ошибки скриптов помечают вкладку
        if (ImGui::BeginTabItem(newErrors ? "Console (!)###console" : "Console###console")) { seenLines = nls::scripts.console.size(); consoleContent(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Граница между панелями, которую тянут мышью. value меняется на сдвиг мыши * sign.
static bool splitter(const char* id, float x, float y, float w, float h, bool vertical, float* value, float sign, float low, float high) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::InvisibleButton("##grip", ImVec2(w, h));
    bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    if (active || hovered) {
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(72, 143, 230, active ? 255 : 140));
    }
    if (active) *value = std::clamp(*value + sign * (vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y), low, high);
    bool released = ImGui::IsItemDeactivated();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return released;
}


// --- Инспектор ---
// =================================================================
static void undoOnEdit() { if (ImGui::IsItemActivated()) saveUndo(); }

static void renderComponents(SceneNode& node, int nodeIdx);

static void renderInspector(bool gameplay) {
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
            if (type == SDF_VOLUME) {
                int res = node.volume ? node.volume->resolution : 0;
                ImGui::Text("Voxel volume %d^3 (%d KB)", res, res * res * res * 4 / 1024);
                ImGui::TextDisabled("Made from a mesh. Object > Convert to Mesh turns it back.");
            } else if (ImGui::BeginCombo("Shape", shapeName(type))) {
                for (int t = 0; t < SDF_PRIMITIVE_COUNT; t++) {
                    if (ImGui::Selectable(shapeName(t), t == type)) { saveUndo(); node.gpuData.position.w = (float)t; node.gpuData.rotationEuler.w = 0.0f; }
                }
                ImGui::EndCombo();
            }
            const char* ops[] = { "Union", "Subtract", "Intersect" };
            int op = std::clamp((int)std::lround(node.gpuData.params.y), 0, 2);
            if (ImGui::Combo("Operation", &op, ops, 3)) { saveUndo(); node.gpuData.params.y = (float)op; }
            if (type != SDF_VOLUME) { ImGui::DragFloat("Radius/Size", &node.gpuData.params.x, 0.05f, 0.01f, 100.0f); undoOnEdit(); }
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

    if (type == NODE_CAMERA && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Field of view", &node.cameraFov, 20.0f, 120.0f, "%.0f deg"); undoOnEdit();
        ImGui::TextDisabled("Play Mode looks through the first active camera.");
    }

    if (gameplay) renderComponents(node, activeIdx);
}

// =================================================================
// --- Компоненты: всё, что делает из формы игровой объект ---
// =================================================================

// Заголовок компонента с кнопкой удаления; false — блок свёрнут
static bool componentHeader(const char* title, bool* removed) {
    ImGui::PushID(title);
    bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 22.0f);
    if (ImGui::SmallButton("x")) *removed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
    ImGui::PopID();
    return open;
}

static void scriptVariables(SceneNode& node) {
    // Список переменных следует за текстом скрипта (компиляция кэшируется по времени файла)
    std::vector<nls::CompileError> errors;
    nls::registerEngineBindings();
    if (!nls::scripts.syncVars(node, &errors)) {
        for (const auto& e : errors) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d:%d %s", e.line, e.column, e.message.c_str());
    }
    for (size_t i = 0; i < node.script.vars.size(); i++) {
        ScriptVar& v = node.script.vars[i];
        ImGui::PushID((int)i);
        switch (v.type) {
            case SVAR_FLOAT: ImGui::DragFloat(v.name.c_str(), &v.value.x, 0.05f); undoOnEdit(); break;
            case SVAR_INT: { int x = (int)std::lround(v.value.x); if (ImGui::DragInt(v.name.c_str(), &x)) v.value.x = (float)x; undoOnEdit(); break; }
            case SVAR_BOOL: { bool b = v.value.x != 0.0f; if (ImGui::Checkbox(v.name.c_str(), &b)) { saveUndo(); v.value.x = b ? 1.0f : 0.0f; } break; }
            case SVAR_VEC3: ImGui::DragFloat3(v.name.c_str(), &v.value.x, 0.05f); undoOnEdit(); break;
            case SVAR_STRING: {
                char buf[256];
                strncpy(buf, v.text.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
                if (ImGui::InputText(v.name.c_str(), buf, sizeof(buf))) v.text = buf;
                undoOnEdit();
                break;
            }
            case SVAR_NODE: {
                const char* current = (v.node >= 0 && v.node < (int)sceneGraph.size()) ? sceneGraph[v.node].name.c_str() : "(none)";
                if (ImGui::BeginCombo(v.name.c_str(), current)) {
                    if (ImGui::Selectable("(none)", v.node == -1)) { saveUndo(); v.node = -1; }
                    for (int n = 0; n < (int)sceneGraph.size(); n++) {
                        ImGui::PushID(n);
                        if (ImGui::Selectable(sceneGraph[n].name.c_str(), v.node == n)) { saveUndo(); v.node = n; }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
        }
        ImGui::PopID();
    }
}

static void renderComponents(SceneNode& node, int nodeIdx) {
    bool isSdf = isSdfNode(node);
    bool removed = false;

    // --- Collider ---
    bool hasCollider = node.collider.shape != COLLIDER_NONE && (isSdf || node.collider.shape != COLLIDER_SHAPE);
    if (hasCollider || node.isTrigger) {
        if (componentHeader("Collider", &removed)) {
            const char* shapes[] = { "Own shape (exact)", "Box", "Sphere", "Capsule" };
            int shape = std::clamp(node.collider.shape, 0, 3);
            if (ImGui::BeginCombo("Shape", shapes[shape])) {
                for (int s = isSdf ? 0 : 1; s < 4; s++) {
                    if (ImGui::Selectable(shapes[s], s == shape)) { saveUndo(); node.collider.shape = s; }
                }
                ImGui::EndCombo();
            }
            if (node.collider.shape != COLLIDER_SHAPE) { ImGui::DragFloat3("Size", &node.collider.size.x, 0.02f, 0.05f, 20.0f); undoOnEdit(); }
            bool trigger = node.isTrigger;
            if (ImGui::Checkbox("Is trigger (pass through, fire events)", &trigger)) { saveUndo(); node.isTrigger = trigger; }
            if (node.isTrigger) {
                int target = node.triggerTarget;
                const char* preview = (target >= 0 && target < (int)sceneGraph.size()) ? sceneGraph[target].name.c_str() : "(none)";
                if (ImGui::BeginCombo("Target", preview)) {
                    if (ImGui::Selectable("(none)", target == -1)) { saveUndo(); node.triggerTarget = -1; }
                    for (int i = 0; i < (int)sceneGraph.size(); i++) {
                        if (i == nodeIdx) continue;
                        ImGui::PushID(i);
                        if (ImGui::Selectable(sceneGraph[i].name.c_str(), target == i)) { saveUndo(); node.triggerTarget = i; }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                const char* actions[] = { "Toggle visibility", "Start behavior", "Stop behavior", "Toggle behavior" };
                int action = node.triggerAction;
                if (ImGui::Combo("On enter", &action, actions, TRIGGER_ACTION_COUNT)) { saveUndo(); node.triggerAction = action; }
                ImGui::TextDisabled("Scripts get triggerEnter / triggerExit.");
            }
        }
        if (removed) { saveUndo(); node.collider.shape = COLLIDER_NONE; node.isTrigger = false; removed = false; }
    }

    // --- Rigid Body ---
    if (node.body.type == BODY_DYNAMIC) {
        if (componentHeader("Rigid Body", &removed)) {
            ImGui::DragFloat("Mass", &node.body.mass, 0.05f, 0.01f, 1000.0f, "%.2f kg"); undoOnEdit();
            ImGui::SliderFloat("Bounciness", &node.body.restitution, 0.0f, 1.0f); undoOnEdit();
            ImGui::SliderFloat("Friction", &node.body.friction, 0.0f, 1.5f); undoOnEdit();
            ImGui::DragFloat("Linear damping", &node.body.linearDamping, 0.01f, 0.0f, 10.0f); undoOnEdit();
            ImGui::DragFloat("Angular damping", &node.body.angularDamping, 0.01f, 0.0f, 10.0f); undoOnEdit();
            ImGui::DragFloat("Gravity scale", &node.body.gravityScale, 0.05f, -5.0f, 5.0f); undoOnEdit();
            if (!hasCollider) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Needs a Collider to be simulated");
        }
        if (removed) { saveUndo(); node.body.type = BODY_STATIC; removed = false; }
    }

    // --- Character Controller ---
    if (node.controller.enabled) {
        if (componentHeader("Character Controller", &removed)) {
            CharacterController& c = node.controller;
            ImGui::DragFloat("Walk speed", &c.walkSpeed, 0.1f, 0.0f, 50.0f); undoOnEdit();
            ImGui::DragFloat("Run speed", &c.runSpeed, 0.1f, 0.0f, 50.0f); undoOnEdit();
            ImGui::DragFloat("Jump speed", &c.jumpSpeed, 0.1f, 0.0f, 50.0f); undoOnEdit();
            ImGui::DragFloat("Radius", &c.radius, 0.01f, 0.05f, 5.0f); undoOnEdit();
            ImGui::DragFloat("Height", &c.height, 0.01f, 0.1f, 10.0f); undoOnEdit();
            bool look = c.mouseLook;
            if (ImGui::Checkbox("Mouse look", &look)) { saveUndo(); c.mouseLook = look; }
            ImGui::TextDisabled("WASD, Shift, Space. Put a Camera inside for a first-person view.");
        }
        if (removed) { saveUndo(); node.controller.enabled = false; removed = false; }
    }

    // --- Audio Source ---
    if (node.audio.enabled) {
        if (componentHeader("Audio Source", &removed)) {
            AudioSource& a = node.audio;
            const char* preview = a.clip.empty() ? "(none)" : a.clip.c_str();
            if (ImGui::BeginCombo("Clip", preview)) {
                for (const char* ext : { ".wav", ".mp3", ".flac" }) {
                    for (const std::string& file : project.list("assets/sounds", ext)) {
                        std::string rel = "assets/sounds/" + file;
                        if (ImGui::Selectable(file.c_str(), rel == a.clip)) { saveUndo(); a.clip = rel; }
                    }
                }
                ImGui::EndCombo();
            }
            if (a.clip.empty()) ImGui::TextDisabled("File > Import > Sound adds clips to the project");
            ImGui::SliderFloat("Volume", &a.volume, 0.0f, 2.0f); undoOnEdit();
            bool loop = a.loop, onStart = a.playOnStart, spatial = a.spatial;
            if (ImGui::Checkbox("Loop", &loop)) { saveUndo(); a.loop = loop; }
            if (ImGui::Checkbox("Play on start", &onStart)) { saveUndo(); a.playOnStart = onStart; }
            if (ImGui::Checkbox("3D sound", &spatial)) { saveUndo(); a.spatial = spatial; }
            if (a.spatial) { ImGui::DragFloat("Range", &a.range, 0.1f, 1.0f, 200.0f); undoOnEdit(); }
        }
        if (removed) { saveUndo(); node.audio.enabled = false; removed = false; }
    }

    // --- Script ---
    if (!node.script.path.empty()) {
        if (componentHeader("Script", &removed)) {
            ImGui::TextUnformatted(node.script.path.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) openScriptInEditor(node.script.path);
            scriptVariables(node);
        }
        if (removed) { saveUndo(); node.script.path.clear(); node.script.vars.clear(); removed = false; }
    }

    // --- Behavior ---
    if (node.behavior != BEHAVIOR_NONE) {
        if (componentHeader("Behavior", &removed)) {
            const char* behaviors[] = { "None", "Rotate", "Ping-Pong" };
            int behavior = node.behavior;
            if (ImGui::Combo("Type", &behavior, behaviors, BEHAVIOR_COUNT)) { saveUndo(); node.behavior = behavior; }
            ImGui::DragFloat3(node.behavior == BEHAVIOR_ROTATE ? "Axis speed" : "Offset", &node.behaviorVec.x, 0.05f); undoOnEdit();
            ImGui::DragFloat("Speed", &node.behaviorSpeed, 0.05f); undoOnEdit();
            bool on = node.behaviorOn;
            if (ImGui::Checkbox("Running at start", &on)) { saveUndo(); node.behaviorOn = on; }
        }
        if (removed) { saveUndo(); node.behavior = BEHAVIOR_NONE; removed = false; }
    }

    // --- Animation ---
    if (node.animation.enabled) {
        if (componentHeader("Animation", &removed)) drawAnimationComponent(node, nodeIdx);
        if (removed) { saveUndo(); node.animation = NodeAnimation(); removed = false; }
    }

    // --- Particles ---
    if (node.emitter.enabled) {
        if (componentHeader("Particles", &removed)) drawParticlesComponent(node, nodeIdx);
        if (removed) { saveUndo(); node.emitter = ParticleEmitter(); removed = false; }
    }

    // --- Add Component ---
    ImGui::Spacing();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width * 0.15f);
    if (ImGui::Button("Add Component", ImVec2(width * 0.7f, 28))) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (ImGui::MenuItem("Collider", nullptr, false, !hasCollider)) { saveUndo(); node.collider.shape = isSdf ? COLLIDER_SHAPE : COLLIDER_BOX; }
        if (ImGui::MenuItem("Trigger Zone", nullptr, false, !node.isTrigger)) { saveUndo(); node.isTrigger = true; if (node.collider.shape == COLLIDER_NONE) node.collider.shape = isSdf ? COLLIDER_SHAPE : COLLIDER_BOX; }
        if (ImGui::MenuItem("Rigid Body", nullptr, false, node.body.type != BODY_DYNAMIC)) {
            saveUndo();
            node.body.type = BODY_DYNAMIC;
            if (!hasCollider) node.collider.shape = isSdf ? COLLIDER_SHAPE : COLLIDER_BOX;   // Без коллайдера тело не симулируется
        }
        if (ImGui::MenuItem("Character Controller", nullptr, false, !node.controller.enabled)) { saveUndo(); node.controller.enabled = true; }
        if (ImGui::MenuItem("Audio Source", nullptr, false, !node.audio.enabled)) { saveUndo(); node.audio.enabled = true; }
        if (ImGui::MenuItem("Behavior", nullptr, false, node.behavior == BEHAVIOR_NONE)) { saveUndo(); node.behavior = BEHAVIOR_ROTATE; }
        if (ImGui::MenuItem("Animation", nullptr, false, !node.animation.enabled)) { saveUndo(); node.animation.enabled = true; }
        if (ImGui::MenuItem("Particles", nullptr, false, !node.emitter.enabled)) { saveUndo(); node.emitter.enabled = true; }
        if (ImGui::BeginMenu("Script", node.script.path.empty())) {
            std::vector<std::string> files = project.list("scripts", ".nls");
            for (const std::string& f : project.list("scripts", ".nlsg")) files.push_back(f);   // Визуальные скрипты
            if (files.empty()) ImGui::TextDisabled("No scripts yet: create one in the Script editor");
            for (const std::string& file : files) {
                if (ImGui::MenuItem(file.c_str())) { saveUndo(); node.script.path = "scripts/" + file; node.script.vars.clear(); }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

// =================================================================
// --- Панели раскладки ---
// =================================================================
void drawToolbar(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, guiAlpha));
    ImGui::Begin("Tools", nullptr, PANEL_FLAGS | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
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
        if (toolButton("Grd", editorCfg.showGrid, "Floor grid (G)")) { editorCfg.showGrid = !editorCfg.showGrid; saveSettings(settingsPath()); }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void drawHierarchyPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin(editingModel() ? "Model Parts" : "Scene Collection", nullptr, PANEL_FLAGS);
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
}

void drawInspectorPanel(float x, float y, float w, float h, bool gameplay) {
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Inspector", nullptr, PANEL_FLAGS);
    renderInspector(gameplay);
    ImGui::End();
}

void drawEditingLayout(GLFWwindow* window, const WorkArea& area, bool gameplay) {
    // Панели вплотную к краям, всё остальное — вьюпорт
    float ui = editorCfg.uiScale;
    float toolbarWidth = showToolbar ? 50.0f * ui : 0.0f;
    float rightPanelWidth = showSidebar ? std::clamp(editorCfg.sidebarWidth * ui, 180.0f, std::max(area.w * 0.6f, 180.0f)) : 0.0f;
    float assetHeight = showAssetBrowser ? std::clamp(editorCfg.bottomHeight * ui, 80.0f, std::max(area.h * 0.7f, 80.0f)) : 0.0f;
    float centerWidth = std::max(area.w - toolbarWidth - rightPanelWidth, 1.0f);
    viewportRect = vec4(area.x + toolbarWidth, area.y, centerWidth, std::max(area.h - assetHeight, 1.0f));

    if (showToolbar) drawToolbar(area.x, area.y, toolbarWidth, area.h);
    if (showSidebar) {
        float rightX = area.x + area.w - rightPanelWidth;
        float hierarchyHeight = area.h * editorCfg.hierarchySplit;
        drawHierarchyPanel(rightX, area.y, rightPanelWidth, hierarchyHeight);
        drawInspectorPanel(rightX, area.y + hierarchyHeight, rightPanelWidth, area.h - hierarchyHeight, gameplay);
    }
    if (showAssetBrowser) drawAssetBrowser(area.x + toolbarWidth, area.y + area.h - assetHeight, centerWidth, assetHeight);

    // Границы панелей тянутся мышью; размеры запоминаются в настройках
    bool resized = false;
    if (showSidebar) {
        float rightX = area.x + area.w - rightPanelWidth;
        resized |= splitter("##splitSidebar", rightX - 3.0f, area.y, 6.0f, area.h, true, &editorCfg.sidebarWidth, -1.0f / ui, 180.0f, 900.0f);
        float split = editorCfg.hierarchySplit * area.h;
        resized |= splitter("##splitHierarchy", rightX + 3.0f, area.y + split - 3.0f, rightPanelWidth - 3.0f, 6.0f, false, &split, 1.0f, area.h * 0.1f, area.h * 0.9f);
        editorCfg.hierarchySplit = split / area.h;
    }
    if (showAssetBrowser) resized |= splitter("##splitBottom", area.x + toolbarWidth, area.y + area.h - assetHeight - 3.0f, centerWidth - 3.0f, 6.0f, false, &editorCfg.bottomHeight, -1.0f / ui, 80.0f, 900.0f);
    if (resized) saveSettings(settingsPath());

    drawViewportOverlays();

    if (triggerSceneContextMenu) { ImGui::OpenPopup("SceneContextMenu"); triggerSceneContextMenu = false; }
    if (ImGui::BeginPopup("SceneContextMenu")) {
        if (ImGui::MenuItem("Duplicate")) { duplicateSelectedObject(); }
        if (ImGui::MenuItem("Delete")) { deleteSelectedObject(); }
        if (ImGui::MenuItem("Convert to Mesh...", nullptr, false, canConvert(CONVERT_TO_MESH))) requestConversion(CONVERT_TO_MESH);
        if (ImGui::MenuItem("Convert to SDF Volume...", nullptr, false, canConvert(CONVERT_TO_VOLUME))) requestConversion(CONVERT_TO_VOLUME);
        if (ImGui::MenuItem("Edit in Blender", nullptr, false, canEditInBlender())) editInBlender();
        if (ImGui::BeginMenu("Add")) { addMenuItems(window); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }
}

static void DrawSceneNode(int nodeIdx) {
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
