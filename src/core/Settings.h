#pragma once
#include <string>

// Пресеты качества: от «картошки» до «ультра»
enum QualityPreset { PRESET_POTATO = 0, PRESET_LOW, PRESET_MEDIUM, PRESET_HIGH, PRESET_ULTRA, PRESET_CUSTOM, PRESET_COUNT };
enum ShadowMode { SHADOWS_OFF = 0, SHADOWS_HARD, SHADOWS_SOFT };

struct GraphicsSettings {
    int preset = PRESET_MEDIUM;
    float renderScale = 1.0f;         // Масштаб разрешения неподвижного кадра
    float interactiveScale = 0.5f;    // Масштаб во время движения (если autoScale выключен)
    bool autoScale = true;            // Подбирать масштаб движения под targetFps
    int targetFps = 60;
    bool vsync = true;
    int maxSteps = 96;                // Шаги raymarching
    int shadowMode = SHADOWS_SOFT;
    int shadowSteps = 24;
    bool ao = true;
    int bounces = 2;                  // 1 = без отражений
    bool lightShadows = true;         // Тени от точечных источников
    bool simplifyOnInteract = false;  // Во время движения рисовать в режиме Solid
    bool idleThrottle = true;         // Не крутить кадры, пока ничего не происходит
    int gpuIndex = -1;                // -1 = автовыбор
};

struct EditorSettings {
    bool showGrid = true;
    bool snap = false;
    float snapStep = 0.5f;
    float snapAngleDeg = 15.0f;
    float fovDeg = 45.0f;
    bool showStats = true;
    float uiScale = 1.0f;             // Масштаб интерфейса: шрифт и отступы (применяется при запуске)
    float sidebarWidth = 320.0f;      // Размеры панелей — их двигают мышью за границы
    float bottomHeight = 180.0f;
    float hierarchySplit = 0.4f;      // Доля боковой панели под иерархию
    std::string projectDir = "project";
    std::string blenderPath = "blender";   // Для Object > Edit in Blender   // Папка проекта: assets/, scenes/, scripts/
    std::string lastVersion;          // Для показа changelog один раз на версию
};

extern GraphicsSettings gfx;
extern EditorSettings editorCfg;
extern bool gfxSwapchainDirty;   // vsync поменяли — нужен новый swapchain

const char* presetName(int preset);
void applyPreset(int preset);
bool loadSettings(const std::string& path);   // false, если файла нет
void saveSettings(const std::string& path);
std::string settingsPath();
