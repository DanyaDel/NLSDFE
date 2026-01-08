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
#include <filesystem> // <-- ВАЖНО: Для проверки файла настроек

// Гарантируем доступ к ID логотипа
extern void* g_AppLogoID; 

// Изначально FALSE. Мы включим его программно, только если это первый запуск.
static bool showAboutWindow = false; 
static bool isFirstRunCheckDone = false; // Флаг, чтобы проверка прошла только 1 раз за сессию

struct ChangelogEntry {
    std::string version;
    std::string date;
    std::vector<std::string> changes;
    ChangelogEntry(std::string v, std::string d, std::vector<std::string> c) 
        : version(v), date(d), changes(c) {}
};

const std::vector<ChangelogEntry> appChangelog = {
    ChangelogEntry(
        "v1.3 - Usability Fixes", "08.01.2026",
        {
            "Fixed Gizmo position logic for multi-object selection.",
            "Renamed 'Redactor' to 'Editor' in UI.",
            "Improved 'Move All' (Center) gizmo handle.",
            "Guide lines are now thinner and longer.",
            "Added About Engine window."
            "Restored SDF Geometry settings in Inspector.",
            "Gizmo is now always drawn on top of objects (X-Ray).",
            "Added key repeat for Undo/Redo/Paste.",
            "Fixed Gizmo priority issues."
        }
    ),
    ChangelogEntry(
        "v1.2 - Modular Update", "08.01.2026",
        {
            "Refactored engine into modules.",
            "Fixed LNK2005 duplicate symbol errors.",
            "Moved Gizmo logic to Input.cpp.",
            "Implemented Changelog system."
        }
    ),
    ChangelogEntry(
        "v1.1 - Hotfix", "05.01.2026",
        {
            "Fixed zero-scale crash.",
            "Optimized NOS file size for thumbnails.",
            "Added Shift-acceleration for camera movement."
        }
    ),
    ChangelogEntry(
        "v1.0 - Initial Release", "01.01.2026",
        {
            "Basic Vulkan Raymarching core.",
            "NOS file format support.",
            "Primitive types: Sphere and Box."
        }
    )
};

void TextCentered(std::string text) {
    float winWidth = ImGui::GetWindowSize().x;
    float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.c_str());
}

// --- ОКНО ABOUT ---
void renderAboutWindow() {
    if (!showAboutWindow) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size = ImVec2(io.DisplaySize.x * 0.9f, io.DisplaySize.y * 0.9f);
    
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - size.x) / 2.0f, (io.DisplaySize.y - size.y) / 2.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    if (ImGui::Begin("About Engine", &showAboutWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Dummy(ImVec2(0, 50));

        // Заголовок
        ImGui::SetWindowFontScale(2.0f); 
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28f, 0.56f, 0.90f, 1.0f)); 
        TextCentered("NEXT LEVEL SDF ENGINE");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f); 

        ImGui::Dummy(ImVec2(0, 10));
        TextCentered("Next Level Signed Distance Field Technology");
        ImGui::Dummy(ImVec2(0, 30));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 30));

        // Текст
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); 
        float textWidth = 600.0f;
        if (textWidth > size.x - 40) textWidth = size.x - 40;

        ImGui::SetCursorPosX((size.x - textWidth) * 0.5f);
        ImGui::BeginGroup();
            
            ImGui::SetCursorPosX((textWidth - ImGui::CalcTextSize("THEORY OF OPERATION").x) * 0.5f);
            ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "THEORY OF OPERATION");
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textWidth);
            ImGui::TextWrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 30));

            ImGui::SetCursorPosX((textWidth - ImGui::CalcTextSize("RAYMARCHING CORE").x) * 0.5f);
            ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "RAYMARCHING CORE");
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textWidth);
            ImGui::TextWrapped("Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium.");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 30));

            ImGui::SetCursorPosX((textWidth - ImGui::CalcTextSize("FUTURE PLANS").x) * 0.5f);
            ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.0f), "FUTURE PLANS");
            ImGui::Dummy(ImVec2(0, 5));
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textWidth);
            ImGui::TextWrapped("Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt.");
            ImGui::PopTextWrapPos();

        ImGui::EndGroup();
        ImGui::PopStyleColor();

        // Кнопка
        float buttonW = 200;
        float buttonH = 40;
        ImGui::SetCursorPosY(size.y - buttonH - 30);
        ImGui::SetCursorPosX((size.x - buttonW) * 0.5f);
        if (ImGui::Button("Welcome to NLSDFE!", ImVec2(buttonW, buttonH))) {
            showAboutWindow = false;
        }
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
    
    // --- ЛОГИКА ПЕРВОГО ЗАПУСКА ---
    // Если это первый кадр и файла настроек НЕТ, значит это "чистая установка".
    if (!isFirstRunCheckDone) {
        if (!std::filesystem::exists("imgui.ini")) {
            showAboutWindow = true; // Показать приветствие
        }
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
            if (ImGui::BeginMenu("Recent Project Info")) {
                ImGui::Text("File: %s", currentLoadedFile.c_str());
                if (currentLoadedFile != "Untitled") { ImGui::Text("Objects: %d", lastLoadedHeader.objectCount); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Changelog")) showChangelog = true;
                if (ImGui::MenuItem("About Engine")) showAboutWindow = true;
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) { glfwSetWindowShouldClose(window, true); }
            ImGui::TextDisabled("NLSDFE v1.4");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Properties")) { showSettingsWindow = true; }
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) { performUndo(); }
            ImGui::EndMenu();
        }

        float buttonWidth = 25.0f;
        float totalWidth = 4 * buttonWidth + 3 * ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetWindowWidth() - totalWidth - 20.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f); 
        const char* labels[] = {"W", "S", "M", "R"};
        for(int i=0; i<4; i++) {
            if (currentShadingMode == i) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            if (ImGui::Button(labels[i], ImVec2(buttonWidth, 0))) currentShadingMode = i;
            if (currentShadingMode == i) ImGui::PopStyleColor();
            if(i < 3) ImGui::SameLine();
        }
        ImGui::PopStyleVar();
        ImGui::EndMainMenuBar();
    }

    float headerHeight = 28.0f;
    if (showToolSettings) {
        ImGui::SetNextWindowPos(ImVec2(0, menuHeight));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, headerHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, guiAlpha)); 
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 4));
        ImGui::Begin("HeaderStrip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        {
            if(ImGui::Button("View")) { ImGui::OpenPopup("HeaderViewMenu"); }
            if (ImGui::BeginPopup("HeaderViewMenu")) {
                ImGui::MenuItem("Toolbar", "T", &showToolbar); ImGui::MenuItem("Sidebar", "N", &showSidebar); ImGui::MenuItem("Tool Settings", NULL, &showToolSettings); ImGui::EndPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Select")) { ImGui::OpenPopup("SelectMenu"); }
            if (ImGui::BeginPopup("SelectMenu")) { if(ImGui::MenuItem("All", "A")) { selectedIndices.resize(sceneObjects.size()); std::iota(selectedIndices.begin(), selectedIndices.end(), 0); } if(ImGui::MenuItem("None", "Alt+A")) { selectedIndices.clear(); } ImGui::EndPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Add")) { ImGui::OpenPopup("AddMenu"); }
            if (ImGui::BeginPopup("AddMenu")) { if (ImGui::BeginMenu("Mesh")) { if (ImGui::MenuItem("Sphere")) { saveUndo(); if (sceneObjects.size() < MAX_OBJECTS) { mat4 id; memset(&id,0,sizeof(id)); id.m[0]=1;id.m[5]=1;id.m[10]=1;id.m[15]=1; sceneObjects.push_back({id, {0,2,0,0}, {1,1,1,0}, {1.0f,0,0,0}, {1,1,1,1}, {0,0,0,0}}); selectedIndices.clear(); selectedIndices.push_back((int)sceneObjects.size() - 1); } } if (ImGui::MenuItem("Cube")) { saveUndo(); if (sceneObjects.size() < MAX_OBJECTS) { mat4 id; memset(&id,0,sizeof(id)); id.m[0]=1;id.m[5]=1;id.m[10]=1;id.m[15]=1; sceneObjects.push_back({id, {0,2,0,1}, {1,1,1,0}, {1.0f,0,0,0}, {1,1,1,1}, {0,0,0,0}}); selectedIndices.clear(); selectedIndices.push_back((int)sceneObjects.size() - 1); } } ImGui::EndMenu(); } ImGui::EndPopup(); }
            ImGui::SameLine();
            if(ImGui::Button("Object")) { ImGui::OpenPopup("ObjectMenu"); } 
            if (ImGui::BeginPopup("ObjectMenu")) { if(ImGui::MenuItem("Duplicate Objects", "Alt+D")) { duplicateSelectedObject(); } if(ImGui::MenuItem("Delete", "X")) { deleteSelectedObject(); } ImGui::EndPopup(); }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    } else { headerHeight = 0.0f; }

    float toolbarWidth = showToolbar ? 50.0f : 0.0f;
    float rightPanelWidth = showSidebar ? 320.0f : 0.0f;
    float margin = 10.0f;
    float topOffset = menuHeight + headerHeight + (showToolSettings ? margin : 0); 
    float workspaceHeight = io.DisplaySize.y - topOffset;
    auto SetWindowLayout = [&](float x, float y, float w, float h) { ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h)); };

    if (showToolbar) {
        SetWindowLayout(0, topOffset, toolbarWidth, workspaceHeight);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, guiAlpha));
        ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
            ImGui::Button("Sel", ImVec2(30, 30)); ImGui::Separator();
            bool isMov = (currentMode == MODE_MOVE);
            if(isMov) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if(ImGui::Button("Mov", ImVec2(30, 30))) currentMode = MODE_MOVE;
            if(isMov) ImGui::PopStyleColor();
            bool isRot = (currentMode == MODE_ROTATE);
            if(isRot) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if(ImGui::Button("Rot", ImVec2(30, 30))) currentMode = MODE_ROTATE;
            if(isRot) ImGui::PopStyleColor();
            bool isScl = (currentMode == MODE_SCALE);
            if(isScl) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if(ImGui::Button("Scl", ImVec2(30, 30))) currentMode = MODE_SCALE;
            if(isScl) ImGui::PopStyleColor();
            ImGui::Separator();
            bool isGiz = showGizmos;
            if (isGiz) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("Giz", ImVec2(30, 30))) showGizmos = !showGizmos;
            if (isGiz) ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    float rightX = io.DisplaySize.x - rightPanelWidth - margin;
    if (showSidebar) {
        float hierarchyHeight = workspaceHeight * 0.4f - margin * 1.5f; 
        float inspectorHeight = workspaceHeight * 0.6f - margin * 1.5f;
        SetWindowLayout(rightX, topOffset, rightPanelWidth, hierarchyHeight);
        ImGui::Begin("Scene Collection", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        {
            ImGui::TextDisabled("HIERARCHY"); ImGui::Separator();
            ImGui::BeginChild("ScrollH", ImVec2(0,0), false);
            for (int i = 0; i < sceneObjects.size(); i++) {
                std::string label = ((int)sceneObjects[i].position.w == 0 ? "Sphere " : "Box ") + std::to_string(i);
                bool isSelected = false;
                for(int s : selectedIndices) if(s == i) isSelected = true;
                if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28f, 0.56f, 0.90f, 1.00f));
                if (ImGui::Selectable(label.c_str(), isSelected)) { if(!ImGui::GetIO().KeyShift) selectedIndices.clear(); selectedIndices.push_back(i); }
                if (isSelected) ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        }
        ImGui::End();
        SetWindowLayout(rightX, topOffset + hierarchyHeight + margin * 2, rightPanelWidth, inspectorHeight);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
        {
            if (!selectedIndices.empty()) {
                int activeIdx = selectedIndices.back();
                if (activeIdx < sceneObjects.size()) {
                    ObjectData& obj = sceneObjects[activeIdx];
                    ImGui::TextColored(ImVec4(0.28f, 0.56f, 0.90f, 1.00f), "Active Object %d", activeIdx);
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Dummy(ImVec2(0, 5));
                        ImGui::Text("Position"); ImGui::DragFloat3("##Pos", &obj.position.x, 0.05f);
                        ImGui::Text("Rotation");
                        if (ImGui::DragFloat3("##Rot", &obj.rotationEuler.x, 0.05f)) { 
                            vec3 r = {obj.rotationEuler.x, obj.rotationEuler.y, obj.rotationEuler.z}; 
                            obj.invRotation = createInverseRotationMatrix4x4(r); 
                        }
                        ImGui::Text("Scale"); ImGui::DragFloat3("##Scale", &obj.scale.x, 0.05f);
                    }
                    if (ImGui::CollapsingHeader("Geometry (SDF)", ImGuiTreeNodeFlags_DefaultOpen)) {
                         const char* shapes[] = { "Sphere", "Box" };
                         int type = (int)obj.position.w;
                         if (ImGui::Combo("Shape", &type, shapes, IM_ARRAYSIZE(shapes))) obj.position.w = (float)type;
                         const char* ops[] = { "Union", "Subtract", "Intersect" };
                         int op = (int)obj.params.y;
                         if (ImGui::Combo("Blend Op", &op, ops, IM_ARRAYSIZE(ops))) obj.params.y = (float)op;
                         ImGui::DragFloat("Smoothness", &obj.params.z, 0.01f, 0.0f, 2.0f);
                         ImGui::DragFloat("Radius/Size", &obj.params.x, 0.05f, 0.001f, 100.0f);
                    }
                    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::ColorEdit3("Color", &obj.color.x);
                        ImGui::SliderFloat("Metallic", &obj.params.w, 0.0f, 1.0f);
                        ImGui::SliderFloat("Roughness", &obj.color.w, 0.0f, 1.0f);
                    }
                    if (ImGui::Button("DELETE OBJECT", ImVec2(-1, 30))) { deleteSelectedObject(); }
                }
            } else { ImGui::TextDisabled("Select an object."); }
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
    for (int idx : selectedIndices) {
        if (idx >= sceneObjects.size()) continue;
        vec3 pos = {sceneObjects[idx].position.x, sceneObjects[idx].position.y, sceneObjects[idx].position.z};
        float size = sceneObjects[idx].params.x; 
        vec3 corners[8]; float sx = size * sceneObjects[idx].scale.x; float sy = size * sceneObjects[idx].scale.y; float sz = size * sceneObjects[idx].scale.z;
        for(int i=0; i<8; i++) { vec3 offset = { (i&1?sx:-sx), (i&2?sy:-sy), (i&4?sz:-sz) }; corners[i] = pos + offset; }
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

    if (showSettingsWindow) {
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Preferences", &showSettingsWindow, ImGuiWindowFlags_NoCollapse)) {
            ImGui::BeginChild("Categories", ImVec2(150, 0), true);
            if (ImGui::Selectable("Interface", selectedSettingsTab == 0)) selectedSettingsTab = 0;
            if (ImGui::Selectable("Data", selectedSettingsTab == 1)) selectedSettingsTab = 1; 
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::BeginChild("SettingsContent", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
            if (selectedSettingsTab == 0) { ImGui::Text("User Interface"); ImGui::Separator(); if(ImGui::SliderFloat("Menu Transparency", &guiAlpha, 0.1f, 1.0f, "%.2f")) { setupImGuiStyle(); } } 
            else if (selectedSettingsTab == 1) { ImGui::Text("Project Defaults"); ImGui::Separator(); ImGui::Dummy(ImVec2(0, 10)); ImGui::Text("Default Export Location:"); std::string dispPath = defaultExportPath.empty() ? "(Not set - using working directory)" : defaultExportPath; ImGui::TextWrapped("%s", dispPath.c_str()); }
            ImGui::EndChild(); ImGui::EndGroup(); ImGui::End();
        }
    }

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
    // Темная панель меню для белого логотипа
    colors[ImGuiCol_MenuBarBg]      = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
}