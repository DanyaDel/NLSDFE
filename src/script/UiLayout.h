#pragma once
#include "GameApi.h"
#include <string>
#include <vector>

// Экраны интерфейса (.nlsui): собираются в редакторе (UI Editor), показываются скриптом — ui.show("menu").
// Положение и размеры элементов — в долях экрана, размер текста — в пикселях при высоте экрана 720.
namespace nls::ui {

enum Kind { KIND_TEXT = 0, KIND_PANEL, KIND_BAR, KIND_BUTTON, KIND_COUNT };
const char* kindName(int kind);

struct Element {
    int kind = KIND_TEXT;
    std::string name;            // По имени к элементу обращается скрипт
    float x = 0.4f, y = 0.4f, w = 0.2f, h = 0.08f;
    vec3 color = vec3(1.0f);
    float size = 24.0f;          // Размер текста
    std::string text;
    float value = 1.0f;          // Заполнение полоски 0..1
    bool visible = true;
    // Состояние в игре
    bool hovered = false, pressed = false, clicked = false;
};

struct Layout {
    std::string name;            // Имя, под которым экран показан: "menu" для ui/menu.nlsui
    std::vector<Element> elements;
    Element* find(const std::string& element);
};

bool load(const std::string& path, Layout& out);
bool save(const std::string& path, const Layout& layout);

// --- Игра ---
extern std::vector<Layout> shown;                 // В порядке показа: поздние рисуются поверх
void reset();
bool show(const std::string& name);               // false — файл не найден
void hide(const std::string& name);
Layout* find(const std::string& name);
// Наведение и щелчки по кнопкам; мышь — в долях экрана
void updateInput(float mouseX, float mouseY, bool down, bool pressed);
// Элементы экрана в виде примитивов интерфейса игры
void appendItems(const Layout& layout, std::vector<game::HudItem>& out);

void registerBindings();

}
