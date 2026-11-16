#include "UiLayout.h"
#include "ScriptSystem.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nls::ui {

std::vector<Layout> shown;

namespace {
    using T = Type;
    const char* kindNames[KIND_COUNT] = { "text", "panel", "bar", "button" };

    vec3 vec(const Value& v) { return vec3(v.v[0], v.v[1], v.v[2]); }

    // Элемент экрана для функций скрипта; ошибка скрипта, если экран не показан или имя неверное
    Element* elementOf(VM& vm, const Value& layout, const Value& element) {
        Layout* l = find(vm.string(layout));
        if (!l) { vm.fail("ui screen '" + vm.string(layout) + "' is not shown"); return nullptr; }
        Element* e = l->find(vm.string(element));
        if (!e) vm.fail("ui screen '" + l->name + "' has no element '" + vm.string(element) + "'");
        return e;
    }

    std::string screenFile(const std::string& name) {
        namespace fs = std::filesystem;
        fs::path p(name);
        if (p.extension().empty()) p += ".nlsui";
        std::error_code ec;
        for (const fs::path& candidate : { fs::path("ui") / p, p }) {
            std::string full = host.resolvePath ? host.resolvePath(candidate.generic_string()) : scripts.resolve(candidate.generic_string());
            if (fs::exists(full, ec)) return full;
        }
        return "";
    }
}

const char* kindName(int kind) { return kind >= 0 && kind < KIND_COUNT ? kindNames[kind] : "?"; }

Element* Layout::find(const std::string& element) {
    for (Element& e : elements) if (e.name == element) return &e;
    return nullptr;
}

// Строка файла: kind|name|x|y|w|h|r|g|b|size|value|visible|text  (текст — до конца строки)
bool load(const std::string& path, Layout& out) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    out.elements.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        size_t from = 0;
        while (parts.size() < 12) {
            size_t bar = line.find('|', from);
            if (bar == std::string::npos) break;
            parts.push_back(line.substr(from, bar - from));
            from = bar + 1;
        }
        if (parts.size() != 12) return false;
        Element e;
        e.kind = KIND_COUNT;
        for (int k = 0; k < KIND_COUNT; k++) if (parts[0] == kindNames[k]) e.kind = k;
        if (e.kind == KIND_COUNT) return false;
        e.name = parts[1];
        float* numbers[] = { &e.x, &e.y, &e.w, &e.h, &e.color.x, &e.color.y, &e.color.z, &e.size, &e.value };
        for (int i = 0; i < 9; i++) *numbers[i] = (float)std::atof(parts[2 + i].c_str());
        e.visible = parts[11] != "0";
        e.text = line.substr(from);
        // "\n" в тексте — перенос строки
        for (size_t at; (at = e.text.find("\\n")) != std::string::npos;) e.text.replace(at, 2, "\n");
        out.elements.push_back(e);
    }
    return true;
}

bool save(const std::string& path, const Layout& layout) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "# NLSDFE UI screen: kind|name|x|y|w|h|r|g|b|size|value|visible|text\n";
    for (const Element& e : layout.elements) {
        std::string name = e.name, text = e.text;
        std::replace(name.begin(), name.end(), '|', '_');
        for (size_t at; (at = text.find('\n')) != std::string::npos;) text.replace(at, 1, "\\n");
        out << kindName(e.kind) << '|' << name << '|' << e.x << '|' << e.y << '|' << e.w << '|' << e.h << '|'
            << e.color.x << '|' << e.color.y << '|' << e.color.z << '|' << e.size << '|' << e.value << '|' << (e.visible ? 1 : 0) << '|' << text << '\n';
    }
    return out.good();
}

void reset() { shown.clear(); }

Layout* find(const std::string& name) {
    for (Layout& l : shown) if (l.name == name) return &l;
    return nullptr;
}

bool show(const std::string& name) {
    if (find(name)) return true;
    std::string file = screenFile(name);
    Layout layout;
    if (file.empty() || !load(file, layout)) return false;
    layout.name = name;
    shown.push_back(layout);
    return true;
}

void hide(const std::string& name) {
    shown.erase(std::remove_if(shown.begin(), shown.end(), [&](const Layout& l) { return l.name == name; }), shown.end());
}

void updateInput(float mouseX, float mouseY, bool down, bool pressed) {
    // Щелчок достаётся только верхней кнопке под курсором
    bool taken = false;
    for (int l = (int)shown.size() - 1; l >= 0; l--) {
        auto& elements = shown[l].elements;
        for (int i = (int)elements.size() - 1; i >= 0; i--) {
            Element& e = elements[i];
            e.hovered = e.pressed = e.clicked = false;
            if (e.kind != KIND_BUTTON || !e.visible || taken) continue;
            if (mouseX < e.x || mouseX > e.x + e.w || mouseY < e.y || mouseY > e.y + e.h) continue;
            taken = true;
            e.hovered = true;
            e.pressed = down;
            e.clicked = pressed;
        }
    }
}

void appendItems(const Layout& layout, std::vector<game::HudItem>& out) {
    for (const Element& e : layout.elements) {
        if (!e.visible) continue;
        game::HudItem item;
        item.x = e.x; item.y = e.y; item.w = e.w; item.h = e.h;
        item.color = e.color;
        switch (e.kind) {
            case KIND_TEXT: item.kind = game::HUD_TEXT; item.text = e.text; item.value = e.size; break;
            case KIND_BAR: item.kind = game::HUD_BAR; item.value = std::clamp(e.value, 0.0f, 1.0f); break;
            case KIND_BUTTON:
                item.kind = game::HUD_RECT; item.text = e.text; item.textSize = e.size;
                if (e.pressed) item.color *= 0.7f; else if (e.hovered) item.color = glm::min(item.color * 1.25f + vec3(0.05f), vec3(1.0f));
                break;
            default: item.kind = game::HUD_RECT; break;
        }
        out.push_back(item);
    }
}

void registerBindings() {
    Registry& r = registry();
    // Экран из ui/<имя>.nlsui; повторный показ ничего не меняет
    r.function("ui.show", T::Void, {T::String}, [](VM& vm, Value* a, Value&) {
        if (!show(vm.string(a[0]))) vm.fail("ui screen '" + vm.string(a[0]) + "' not found (expected ui/" + vm.string(a[0]) + ".nlsui)");
    });
    r.function("ui.hide", T::Void, {T::String}, [](VM& vm, Value* a, Value&) { hide(vm.string(a[0])); });
    r.function("ui.isShown", T::Bool, {T::String}, [](VM& vm, Value* a, Value& res) { res = Value::Int(find(vm.string(a[0])) ? 1 : 0); });
    r.function("ui.clicked", T::Bool, {T::String, T::String}, [](VM& vm, Value* a, Value& res) {
        // Скрытый экран — не ошибка: кнопка просто не нажата
        Layout* l = find(vm.string(a[0]));
        Element* e = l ? l->find(vm.string(a[1])) : nullptr;
        if (l && !e) { vm.fail("ui screen '" + l->name + "' has no element '" + vm.string(a[1]) + "'"); return; }
        res = Value::Int(e && e->clicked ? 1 : 0);
    });
    r.function("ui.setText", T::Void, {T::String, T::String, T::String}, [](VM& vm, Value* a, Value&) {
        if (Element* e = elementOf(vm, a[0], a[1])) e->text = vm.string(a[2]);
    });
    r.function("ui.setValue", T::Void, {T::String, T::String, T::Float}, [](VM& vm, Value* a, Value&) {
        if (Element* e = elementOf(vm, a[0], a[1])) e->value = a[2].f;
    });
    r.function("ui.setColor", T::Void, {T::String, T::String, T::Vec3}, [](VM& vm, Value* a, Value&) {
        if (Element* e = elementOf(vm, a[0], a[1])) e->color = vec(a[2]);
    });
    r.function("ui.setVisible", T::Void, {T::String, T::String, T::Bool}, [](VM& vm, Value* a, Value&) {
        if (Element* e = elementOf(vm, a[0], a[1])) e->visible = a[2].i != 0;
    });
}

}
