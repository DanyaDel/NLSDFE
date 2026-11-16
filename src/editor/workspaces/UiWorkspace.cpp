// UI Editor: экраны игрового интерфейса (.nlsui). Элементы двигаются и растягиваются мышью
// на холсте с пропорциями игры; рисует их тот же код, что и в игре, — что видно здесь, то будет на экране.
#include "Panels.h"
#include "Project.h"
#include "UiLayout.h"
#include "Game.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using nls::ui::Element;

namespace {
    std::string openFile;          // Имя файла в ui/: "menu.nlsui"
    nls::ui::Layout layout;
    bool modified = false;
    int selected = -1;
    enum Drag { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };
    int drag = DRAG_NONE;
    ImVec2 dragOffset;             // От курсора до угла элемента, в долях экрана
    bool dragChanged = false;      // Снимок для отмены уже сделан в этом перетаскивании
    bool snap = true;

    // Отмена: снимок экрана перед каждой правкой (перетаскивание — один шаг)
    std::vector<nls::ui::Layout> undoStack, redoStack;
    void remember() {
        undoStack.push_back(layout);
        if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    void step(std::vector<nls::ui::Layout>& from, std::vector<nls::ui::Layout>& to) {
        if (from.empty()) return;
        to.push_back(layout);
        layout = from.back();
        from.pop_back();
        selected = std::min(selected, (int)layout.elements.size() - 1);
        modified = true;
    }

    void load(const std::string& file) {
        nls::ui::Layout loaded;
        if (!nls::ui::load(project.path("ui/" + file), loaded)) { showToast("Cannot read " + file); return; }
        layout = loaded;
        undoStack.clear(); redoStack.clear();
        openFile = file;
        modified = false;
        selected = -1;
    }

    void save() {
        if (openFile.empty()) return;
        std::error_code ec;
        fs::create_directories(project.path("ui"), ec);
        if (!nls::ui::save(project.path("ui/" + openFile), layout)) { showToast("Cannot write " + openFile); return; }
        modified = false;
        showToast("Saved: " + openFile);
    }

    void createScreen() {
        std::error_code ec;
        fs::create_directories(project.path("ui"), ec);
        for (int i = 0; ; i++) {
            std::string file = "screen" + (i ? "_" + std::to_string(i) : std::string()) + ".nlsui";
            if (fs::exists(project.path("ui/" + file), ec)) continue;
            layout = nls::ui::Layout();
            undoStack.clear(); redoStack.clear();
            openFile = file;
            selected = -1;
            save();
            return;
        }
    }

    // Имя, которого ещё нет на экране: по нему к элементу обращается скрипт
    std::string freeName(const char* base) {
        for (int i = 1; ; i++) {
            std::string name = base + std::to_string(i);
            if (!layout.find(name)) return name;
        }
    }

    void addElement(int kind) {
        remember();
        Element e;
        e.kind = kind;
        e.name = freeName(nls::ui::kindName(kind));
        switch (kind) {
            case nls::ui::KIND_TEXT: e.text = "Text"; e.w = 0.2f; e.h = 0.05f; e.size = 28.0f; break;
            case nls::ui::KIND_PANEL: e.color = vec3(0.08f, 0.09f, 0.12f); e.x = 0.3f; e.y = 0.25f; e.w = 0.4f; e.h = 0.5f; break;
            case nls::ui::KIND_BAR: e.color = vec3(0.85f, 0.2f, 0.2f); e.x = 0.03f; e.y = 0.04f; e.w = 0.25f; e.h = 0.03f; e.value = 0.7f; break;
            default: e.text = "Button"; e.color = vec3(0.2f, 0.38f, 0.7f); e.size = 22.0f; break;
        }
        layout.elements.push_back(e);
        selected = (int)layout.elements.size() - 1;
        modified = true;
    }

    float snapped(float v) { return snap ? std::round(v * 100.0f) / 100.0f : v; }

    void drawCanvas() {
        // Холст 16:9, вписанный в окно
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float w = std::max(avail.x - 16.0f, 64.0f), h = w * 9.0f / 16.0f;
        if (h > avail.y - 16.0f) { h = std::max(avail.y - 16.0f, 36.0f); w = h * 16.0f / 9.0f; }
        ImVec2 origin = ImGui::GetCursorScreenPos();
        origin.x += (avail.x - w) * 0.5f; origin.y += (avail.y - h) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h), IM_COL32(46, 58, 78, 255));
        dl->AddRect(origin, ImVec2(origin.x + w, origin.y + h), IM_COL32(120, 130, 150, 255));

        std::vector<nls::game::HudItem> items;
        nls::ui::appendItems(layout, items);
        dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
        drawHudItems(dl, vec4(origin.x, origin.y, w, h), items);
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##canvas", ImVec2(w, h));
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 m((mouse.x - origin.x) / w, (mouse.y - origin.y) / h);
        auto rectOf = [&](const Element& e, ImVec2& a, ImVec2& b) {
            a = ImVec2(origin.x + e.x * w, origin.y + e.y * h);
            b = ImVec2(a.x + e.w * w, a.y + e.h * h);
        };

        if (ImGui::IsItemActivated()) {
            // Сначала уголок выделенного элемента, потом верхний элемент под курсором
            drag = DRAG_NONE;
            if (selected >= 0 && selected < (int)layout.elements.size()) {
                ImVec2 a, b; rectOf(layout.elements[selected], a, b);
                if (std::fabs(mouse.x - b.x) < 8.0f && std::fabs(mouse.y - b.y) < 8.0f) drag = DRAG_RESIZE;
            }
            if (drag == DRAG_NONE) {
                selected = -1;
                for (int i = (int)layout.elements.size() - 1; i >= 0; i--) {
                    const Element& e = layout.elements[i];
                    if (m.x < e.x || m.x > e.x + e.w || m.y < e.y || m.y > e.y + e.h) continue;
                    selected = i;
                    drag = DRAG_MOVE;
                    dragOffset = ImVec2(m.x - e.x, m.y - e.y);
                    break;
                }
            }
        }
        if (ImGui::IsItemActive() && selected >= 0 && drag != DRAG_NONE && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            if (!dragChanged) { remember(); dragChanged = true; }
            Element& e = layout.elements[selected];
            if (drag == DRAG_MOVE) { e.x = snapped(m.x - dragOffset.x); e.y = snapped(m.y - dragOffset.y); }
            else { e.w = std::max(snapped(m.x - e.x), 0.01f); e.h = std::max(snapped(m.y - e.y), 0.01f); }
            modified = true;
        }
        if (!ImGui::IsItemActive()) { drag = DRAG_NONE; dragChanged = false; }

        if (selected >= 0 && selected < (int)layout.elements.size()) {
            ImVec2 a, b; rectOf(layout.elements[selected], a, b);
            dl->AddRect(a, b, IM_COL32(255, 200, 60, 255), 0.0f, 0, 1.5f);
            dl->AddRectFilled(ImVec2(b.x - 5, b.y - 5), ImVec2(b.x + 5, b.y + 5), IM_COL32(255, 200, 60, 255));
        }
    }

    void drawProperties() {
        if (selected < 0 || selected >= (int)layout.elements.size()) { ImGui::TextDisabled("Click an element on the canvas"); return; }
        // Снимок берётся в начале кадра: если в этом кадре что-то поменяют, он и станет шагом отмены
        nls::ui::Layout before = layout;
        Element& e = layout.elements[selected];
        bool changed = false;
        ImGui::Text("%s", nls::ui::kindName(e.kind));
        char name[64]; snprintf(name, sizeof(name), "%s", e.name.c_str());
        if (ImGui::InputText("Name", name, sizeof(name))) { e.name = name; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scripts address the element by this name");
        if (e.kind == nls::ui::KIND_TEXT || e.kind == nls::ui::KIND_BUTTON) {
            char text[512]; snprintf(text, sizeof(text), "%s", e.text.c_str());
            if (e.kind == nls::ui::KIND_TEXT ? ImGui::InputTextMultiline("Text", text, sizeof(text), ImVec2(0, 60)) : ImGui::InputText("Text", text, sizeof(text))) { e.text = text; changed = true; }
            changed |= ImGui::DragFloat("Text size", &e.size, 0.5f, 6.0f, 200.0f, "%.0f");
        }
        changed |= ImGui::DragFloat2("Position", &e.x, 0.005f, -1.0f, 2.0f, "%.3f");
        changed |= ImGui::DragFloat2("Size", &e.w, 0.005f, 0.01f, 2.0f, "%.3f");
        changed |= ImGui::ColorEdit3("Color", &e.color.x);
        if (e.kind == nls::ui::KIND_BAR) changed |= ImGui::SliderFloat("Fill", &e.value, 0.0f, 1.0f);
        changed |= ImGui::Checkbox("Visible at start", &e.visible);

        ImGui::Separator();
        // Порядок в списке — порядок рисования: нижние строки поверх верхних
        if (ImGui::Button("Raise") && selected + 1 < (int)layout.elements.size()) { std::swap(layout.elements[selected], layout.elements[selected + 1]); selected++; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Lower") && selected > 0) { std::swap(layout.elements[selected], layout.elements[selected - 1]); selected--; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            Element copy = layout.elements[selected];
            copy.name = freeName(nls::ui::kindName(copy.kind));
            copy.x += 0.02f; copy.y += 0.02f;
            layout.elements.push_back(copy);
            selected = (int)layout.elements.size() - 1;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete") || (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput)) {
            layout.elements.erase(layout.elements.begin() + selected);
            selected = -1;
            changed = true;
        }
        // Правка поля тянется много кадров (ввод текста, перетаскивание числа) — шаг отмены один на всю правку
        static bool editing = false;
        if (changed) {
            if (!editing) { undoStack.push_back(before); redoStack.clear(); if (undoStack.size() > 100) undoStack.erase(undoStack.begin()); }
            editing = ImGui::IsAnyItemActive();
            modified = true;
        } else if (!ImGui::IsAnyItemActive()) editing = false;
    }
}

void drawUiWorkspace(GLFWwindow* window, const WorkArea& area) {
    (void)window;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    float listWidth = 200.0f, sideWidth = 320.0f;
    float canvasWidth = std::max(area.w - listWidth - sideWidth, 100.0f);

    // --- Экраны проекта ---
    ImGui::SetNextWindowPos(ImVec2(area.x, area.y)); ImGui::SetNextWindowSize(ImVec2(listWidth, area.h));
    ImGui::Begin("UiScreens", nullptr, flags);
    if (ImGui::Button("New screen", ImVec2(-1, 0))) createScreen();
    ImGui::Separator();
    std::vector<std::string> screens = project.list("ui", ".nlsui");
    // Первый заход в режим: сразу открыт первый экран проекта
    if (openFile.empty() && !screens.empty()) load(screens[0]);
    for (const std::string& file : screens) {
        if (ImGui::Selectable(file.c_str(), file == openFile) && file != openFile) load(file);
    }
    ImGui::End();

    // --- Холст ---
    ImGui::SetNextWindowPos(ImVec2(area.x + listWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(canvasWidth, area.h));
    ImGui::Begin("UiCanvas", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (openFile.empty()) {
        ImGui::TextDisabled("Pick a screen on the left or create a new one.");
    } else {
        std::string screen = fs::path(openFile).stem().string();
        ImGui::Text("%s%s", openFile.c_str(), modified ? " *" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save (Ctrl+S)")) save();
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &snap);
        ImGui::SameLine();
        ImGui::TextDisabled("in a script: ui.show(\"%s\")", screen.c_str());
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, true)) { if (ImGui::GetIO().KeyShift) step(redoStack, undoStack); else step(undoStack, redoStack); }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, true)) step(redoStack, undoStack);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(undoStack.empty());
        if (ImGui::SmallButton("Undo")) step(undoStack, redoStack);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redoStack.empty());
        if (ImGui::SmallButton("Redo")) step(redoStack, undoStack);
        ImGui::EndDisabled();
        drawCanvas();
    }
    ImGui::End();

    // --- Элементы и свойства ---
    ImGui::SetNextWindowPos(ImVec2(area.x + area.w - sideWidth, area.y)); ImGui::SetNextWindowSize(ImVec2(sideWidth, area.h));
    ImGui::Begin("UiSide", nullptr, flags);
    if (!openFile.empty()) {
        ImGui::TextUnformatted("Add");
        for (int kind = 0; kind < nls::ui::KIND_COUNT; kind++) {
            if (kind) ImGui::SameLine();
            if (ImGui::Button(nls::ui::kindName(kind))) addElement(kind);
        }
        ImGui::Separator();
        ImGui::BeginChild("UiElements", ImVec2(0, 160), true);
        for (int i = 0; i < (int)layout.elements.size(); i++) {
            const Element& e = layout.elements[i];
            ImGui::PushID(i);
            std::string label = std::string(nls::ui::kindName(e.kind)) + "  " + e.name;
            if (ImGui::Selectable(label.c_str(), i == selected)) selected = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
        drawProperties();

        if (selected >= 0 && selected < (int)layout.elements.size()) {
            // Готовые строки для скрипта под выбранный элемент
            const Element& e = layout.elements[selected];
            std::string screen = fs::path(openFile).stem().string();
            ImGui::Separator();
            ImGui::TextDisabled("In a script:");
            ImGui::PushTextWrapPos(0.0f);
            if (e.kind == nls::ui::KIND_BUTTON) ImGui::TextDisabled("if ui.clicked(\"%s\", \"%s\") { ... }", screen.c_str(), e.name.c_str());
            if (e.kind == nls::ui::KIND_TEXT) ImGui::TextDisabled("ui.setText(\"%s\", \"%s\", \"...\")", screen.c_str(), e.name.c_str());
            if (e.kind == nls::ui::KIND_BAR) ImGui::TextDisabled("ui.setValue(\"%s\", \"%s\", 0.5)", screen.c_str(), e.name.c_str());
            ImGui::TextDisabled("ui.setVisible(\"%s\", \"%s\", false)", screen.c_str(), e.name.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}
