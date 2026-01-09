#include "EditorUI.h"
#include "Common.h"
#include "Scene.h"
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
#include <filesystem>

extern void* g_AppLogoID; 
static bool showAboutWindow = false; 
static bool isFirstRunCheckDone = false; 
static int renamingNodeIdx = -1; // Индекс узла, который мы сейчас переименовываем в иерархии


struct ChangelogEntry {
    std::string version;
    std::string date;
    std::vector<std::string> changes;
    ChangelogEntry(std::string v, std::string d, std::vector<std::string> c) 
        : version(v), date(d), changes(c) {}
};

const std::vector<ChangelogEntry> appChangelog = {
    // Предыдущая версия с исправлениями
ChangelogEntry("v1.4 - Fixes", "08.01.2026", { 
    "Fixed Transform Gizmos", 
    "Fixed Copy/Paste", 
    "Removed deprecated object list" 
}),

// Новая версия с функционалом
ChangelogEntry("v1.5 - Scene Hierarchy", "09.01.2026", { 
    "Implemented Scene Graph", 
    "Added object naming in the hierarchy" 
})};

void DrawSceneNode(int nodeIdx); 

void TextCentered(std::string text) {
    float winWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.c_str());
}

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
        TextCentered("Fix Edition");
        
        float buttonW = 200;
        float buttonH = 40;
        ImGui::SetCursorPosY(size.y - buttonH - 30);
        ImGui::SetCursorPosX((size.x - buttonW) * 0.5f);
        if (ImGui::Button("Close", ImVec2(buttonW, buttonH))) showAboutWindow = false;
    }
    ImGui::End();
}

void renderChangelogWindow() {
    if (!showChangelog) return;
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("What's New?", &showChangelog, ImGuiWindowFlags_NoCollapse)) {
        for (const auto& entry : appChangelog) {
            ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "%s", entry.version.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("(%s)", entry.date.c_str());
            for (const auto& point : entry.changes) ImGui::BulletText("%s", point.c_str());
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }
        if (ImGui::Button("Close")) showChangelog = false;
    }
    ImGui::End();
}

void renderUI(GLFWwindow* window) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    setupImGuiStyle();
    ImGuiIO& io = ImGui::GetIO();
    
    if (!isFirstRunCheckDone) {
        if (!std::filesystem::exists("imgui.ini")) showAboutWindow = true; 
        isFirstRunCheckDone = true;
    }

    float menuHeight = 0.0f;
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
            if (ImGui::MenuItem("Reset Scene")) { resetScene(); }
            if (ImGui::MenuItem("Import .Nos")) { std::string path = FileDialogs::OpenFile(window, "NOS Scene File (*.nos)\0*.nos\0All Files (*.*)\0*.*\0"); if (!path.empty()) loadNos(path); }
            if (ImGui::MenuItem("Export .Nos")) { std::string path = FileDialogs::SaveFile(window, "NOS Scene File (*.nos)\0*.nos\0", defaultExportPath); if (!path.empty()) { if (path.find(".nos") == std::string::npos) path += ".nos"; saveNos(path); } }
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
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) { performUndo(); }
            ImGui::EndMenu();
        }

        float buttonWidth = 25.0f;
        float totalWidth = 4 * buttonWidth + 3 * ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - totalWidth - 20.0f);
        const char* labels[] = {"W", "S", "M", "R"};
        for(int i=0; i<4; i++) {
            if (ImGui::Button(labels[i], ImVec2(buttonWidth, 0))) currentShadingMode = i;
            if(i < 3) ImGui::SameLine();
        }
        ImGui::EndMainMenuBar();
    }

    float headerHeight = 28.0f;
    if (showToolSettings) {
        ImGui::SetNextWindowPos(ImVec2(0, menuHeight));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, headerHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, guiAlpha)); 
        ImGui::Begin("HeaderStrip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
        {
            if(ImGui::Button("View")) { ImGui::OpenPopup("HeaderViewMenu"); }
            if (ImGui::BeginPopup("HeaderViewMenu")) { ImGui::MenuItem("Toolbar", "T", &showToolbar); ImGui::MenuItem("Sidebar", "N", &showSidebar); ImGui::EndPopup(); }
            ImGui::SameLine();
            if(ImGui::Button("Select")) { ImGui::OpenPopup("SelectMenu"); }
            if (ImGui::BeginPopup("SelectMenu")) { if(ImGui::MenuItem("All", "A")) { selectedIndices.resize(sceneGraph.size()); std::iota(selectedIndices.begin(), selectedIndices.end(), 0); } if(ImGui::MenuItem("None", "Alt+A")) { selectedIndices.clear(); } ImGui::EndPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Add")) { ImGui::OpenPopup("AddMenu"); }
            if (ImGui::BeginPopup("AddMenu")) { 
                auto create = [&](const char* n, int t) { 
                    saveUndo(); 
                    if (sceneGraph.size() < MAX_OBJECTS) { 
                        SceneNode sn; sn.name = n; sn.gpuData.position.w = (float)t; 
                        sn.gpuData.params = {1.0f,0,0,0}; sn.gpuData.color={1,1,1,1};
                        sn.localPosition.y = 2.0f;
                        sceneGraph.push_back(sn); 
                        selectedIndices = {(int)sceneGraph.size()-1}; 
                    } 
                };
                if (ImGui::MenuItem("Sphere")) create("Sphere", 0);
                if (ImGui::MenuItem("Cube")) create("Cube", 1);
                ImGui::EndPopup(); 
            }
            ImGui::SameLine();
            if(ImGui::Button("Object")) { ImGui::OpenPopup("ObjectMenu"); } 
            if (ImGui::BeginPopup("ObjectMenu")) { if(ImGui::MenuItem("Duplicate", "Alt+D")) { duplicateSelectedObject(); } if(ImGui::MenuItem("Delete", "X")) { deleteSelectedObject(); } ImGui::EndPopup(); }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    } else { headerHeight = 0.0f; }

    float toolbarWidth = showToolbar ? 50.0f : 0.0f;
    float rightPanelWidth = showSidebar ? 320.0f : 0.0f;
    float margin = 10.0f;
    float topOffset = menuHeight + headerHeight + (showToolSettings ? margin : 0); 
    float workspaceHeight = io.DisplaySize.y - topOffset;

    if (showToolbar) {
        ImGui::SetNextWindowPos(ImVec2(0, topOffset)); ImGui::SetNextWindowSize(ImVec2(toolbarWidth, workspaceHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, guiAlpha));
        ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
        {
            ImGui::Button("Mov", ImVec2(30, 30)); if(ImGui::IsItemClicked()) currentMode = MODE_MOVE;
            ImGui::Button("Rot", ImVec2(30, 30)); if(ImGui::IsItemClicked()) currentMode = MODE_ROTATE;
            ImGui::Button("Scl", ImVec2(30, 30)); if(ImGui::IsItemClicked()) currentMode = MODE_SCALE;
            ImGui::Separator();
            if (ImGui::Button("Giz", ImVec2(30, 30))) showGizmos = !showGizmos;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    float rightX = io.DisplaySize.x - rightPanelWidth - margin;
    if (showSidebar) {
        float hierarchyHeight = workspaceHeight * 0.4f - margin * 1.5f; 
        float inspectorHeight = workspaceHeight * 0.6f - margin * 1.5f;
        ImGui::SetNextWindowPos(ImVec2(rightX, topOffset)); ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, hierarchyHeight));
        ImGui::Begin("Scene Collection", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        {
            ImGui::TextDisabled("HIERARCHY"); 
            ImGui::Separator();
            ImGui::BeginChild("HierarchyScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (int i = 0; i < (int)sceneGraph.size(); i++) {
                if (sceneGraph[i].parent == -1) DrawSceneNode(i);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        
        ImGui::SetNextWindowPos(ImVec2(rightX, topOffset + hierarchyHeight + margin * 2)); ImGui::SetNextWindowSize(ImVec2(rightPanelWidth, inspectorHeight));
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        {
            if (!selectedIndices.empty()) {
                int activeIdx = selectedIndices.back();
                if (activeIdx < sceneGraph.size()) {
                    SceneNode& node = sceneGraph[activeIdx];
                    char nameBuf[128];
                    strncpy(nameBuf, node.name.c_str(), sizeof(nameBuf));
                    ImGui::PushItemWidth(-1); // Растянуть на всю ширину
                    if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) {
                        node.name = nameBuf;
                    }
                    ImGui::PopItemWidth();
                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                        const char* shapes[] = { "Sphere", "Box" };
                        int type = (int)node.gpuData.position.w;
                        if (ImGui::Combo("Shape", &type, shapes, 2)) node.gpuData.position.w = (float)type;
                        ImGui::DragFloat("Radius/Size", &node.gpuData.params.x, 0.05f);
                        ImGui::DragFloat("Smoothness", &node.gpuData.params.z, 0.01f, 0.0f, 2.0f);
                    }
                    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::ColorEdit3("Color", &node.gpuData.color.x);
                        ImGui::SliderFloat("Metallic", &node.gpuData.params.w, 0.0f, 1.0f);
                    }
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::DragFloat3("Pos", &node.localPosition.x, 0.05f);
                        ImGui::DragFloat3("Rot", &node.localRotation.x, 0.05f); 
                        ImGui::DragFloat3("Scale", &node.localScale.x, 0.05f);
                    }
                }
            } else {
                ImGui::TextDisabled("Select an object in hierarchy");
            }
        }
        ImGui::End();
    }

    if (isBoxSelecting) {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImVec2 p1(std::min((float)mousePressX, (float)currentMouseX), std::min((float)mousePressY, (float)currentMouseY));
        ImVec2 p2(std::max((float)mousePressX, (float)currentMouseX), std::max((float)mousePressY, (float)currentMouseY));
        drawList->AddRect(p1, p2, IM_COL32(255, 255, 255, 150), 0.0f, 0, 1.0f); 
        drawList->AddRectFilled(p1, p2, IM_COL32(255, 255, 255, 30)); 
    }

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    int w, h; glfwGetFramebufferSize(window, &w, &h);
    // Рисуем рамки выделения
    for (int idx : selectedIndices) {
        if (idx >= sceneGraph.size()) continue;
        const auto& od = sceneGraph[idx].gpuData;
        vec3 pos = {od.position.x, od.position.y, od.position.z};
        float size = od.params.x;
        // Для рамки используем мировой масштаб (из gpuData), так как он там уже посчитан
        vec3 scl = {od.scale.x, od.scale.y, od.scale.z}; 
        
        float sx = size * scl.x; 
        float sy = size * scl.y; 
        float sz = size * scl.z;
        
        vec3 corners[8];
        for(int i=0; i<8; i++) { 
            vec3 offset = { (i&1?sx:-sx), (i&2?sy:-sy), (i&4?sz:-sz) }; 
            // Тут упрощенно без вращения рамки, чтобы не усложнять. Для точной рамки нужно умножать offset на матрицу вращения
            corners[i] = pos + offset; 
        }
        ImVec2 minS = {10000, 10000}, maxS = {-10000, -10000}; bool visible = false;
        for(int i=0; i<8; i++) { vec2 sPos; if(projectWorldToScreen(corners[i], (float)w, (float)h, sPos)) { minS.x = std::min(minS.x, sPos.x); minS.y = std::min(minS.y, sPos.y); maxS.x = std::max(maxS.x, sPos.x); maxS.y = std::max(maxS.y, sPos.y); visible = true; } }
        if(visible && minS.x < maxS.x) { dl->AddRect(minS, maxS, IM_COL32(255, 140, 0, 200), 0.0f, 0, 2.0f); }
    }
    
    if (toast.active) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2 - 100, io.DisplaySize.y - 100));
        ImGui::SetNextWindowSize(ImVec2(200, 40));
        ImGui::Begin("Toast", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration);
        TextCentered(toast.message); 
        ImGui::End();
        toast.timer -= io.DeltaTime;
        if (toast.timer <= 0) toast.active = false;
    }

    if (triggerSceneContextMenu) { ImGui::OpenPopup("SceneContextMenu"); triggerSceneContextMenu = false; }
    if (ImGui::BeginPopup("SceneContextMenu")) { if (ImGui::MenuItem("Duplicate")) { duplicateSelectedObject(); } if (ImGui::MenuItem("Delete")) { deleteSelectedObject(); } ImGui::EndPopup(); }

    renderChangelogWindow();
    renderAboutWindow();
    ImGui::Render();
}

void DrawSceneNode(int nodeIdx) {
    if (nodeIdx >= sceneGraph.size()) return;
    SceneNode& node = sceneGraph[nodeIdx];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    
    bool isSelected = false;
    for (int sel : selectedIndices) { if (sel == nodeIdx) { isSelected = true; break; } }
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    // Если этот узел сейчас переименовывается
    if (renamingNodeIdx == nodeIdx) {
        char buf[128];
        strncpy(buf, node.name.c_str(), sizeof(buf));
        ImGui::SetKeyboardFocusHere(); // Автофокус на поле ввода
        if (ImGui::InputText("##rename", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            node.name = buf;
            renamingNodeIdx = -1; // Завершить переименование
        }
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
            renamingNodeIdx = -1; // Сброс, если кликнули мимо
        }
    } else {
        // Обычный вывод узла
        bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)nodeIdx, flags, "%s", node.name.c_str());

        // Двойной клик для начала переименования
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            renamingNodeIdx = nodeIdx;
        }

        if (ImGui::IsItemClicked()) {
            if (!ImGui::GetIO().KeyShift) selectedIndices.clear();
            selectedIndices.push_back(nodeIdx);
        }

        // Контекстное меню для конкретного узла
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename")) { renamingNodeIdx = nodeIdx; }
            if (ImGui::MenuItem("Delete")) { /* логика удаления */ }
            ImGui::EndPopup();
        }

        // ... дальше ваш код с Drag & Drop ...
        if (nodeOpen) {
            for (int childIdx : node.children) DrawSceneNode(childIdx);
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