#include "Settings.h"
#include <fstream>
#include <sstream>
#include <algorithm>

GraphicsSettings gfx;
EditorSettings editorCfg;
bool gfxSwapchainDirty = false;

const char* presetName(int preset) {
    static const char* names[] = { "Potato", "Low", "Medium", "High", "Ultra", "Custom" };
    return names[std::clamp(preset, 0, (int)PRESET_CUSTOM)];
}

void applyPreset(int preset) {
    gfx.preset = preset;
    switch (preset) {
        case PRESET_POTATO:
            gfx.renderScale = 0.5f; gfx.interactiveScale = 0.3f; gfx.maxSteps = 48;
            gfx.shadowMode = SHADOWS_OFF; gfx.shadowSteps = 12; gfx.ao = false; gfx.bounces = 1;
            gfx.lightShadows = false; gfx.simplifyOnInteract = true; break;
        case PRESET_LOW:
            gfx.renderScale = 0.75f; gfx.interactiveScale = 0.4f; gfx.maxSteps = 64;
            gfx.shadowMode = SHADOWS_HARD; gfx.shadowSteps = 16; gfx.ao = false; gfx.bounces = 1;
            gfx.lightShadows = false; gfx.simplifyOnInteract = true; break;
        case PRESET_MEDIUM:
            gfx.renderScale = 1.0f; gfx.interactiveScale = 0.5f; gfx.maxSteps = 96;
            gfx.shadowMode = SHADOWS_SOFT; gfx.shadowSteps = 24; gfx.ao = true; gfx.bounces = 2;
            gfx.lightShadows = true; gfx.simplifyOnInteract = false; break;
        case PRESET_HIGH:
            gfx.renderScale = 1.0f; gfx.interactiveScale = 0.75f; gfx.maxSteps = 128;
            gfx.shadowMode = SHADOWS_SOFT; gfx.shadowSteps = 32; gfx.ao = true; gfx.bounces = 2;
            gfx.lightShadows = true; gfx.simplifyOnInteract = false; break;
        case PRESET_ULTRA:
            gfx.renderScale = 1.0f; gfx.interactiveScale = 1.0f; gfx.maxSteps = 192;
            gfx.shadowMode = SHADOWS_SOFT; gfx.shadowSteps = 48; gfx.ao = true; gfx.bounces = 3;
            gfx.lightShadows = true; gfx.simplifyOnInteract = false; break;
        default: break;
    }
}

std::string settingsPath() { return "nlsdfe.cfg"; }

bool loadSettings(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        try {
            if (k == "preset") gfx.preset = std::stoi(v);
            else if (k == "renderScale") gfx.renderScale = std::stof(v);
            else if (k == "interactiveScale") gfx.interactiveScale = std::stof(v);
            else if (k == "autoScale") gfx.autoScale = std::stoi(v) != 0;
            else if (k == "targetFps") gfx.targetFps = std::stoi(v);
            else if (k == "vsync") gfx.vsync = std::stoi(v) != 0;
            else if (k == "maxSteps") gfx.maxSteps = std::stoi(v);
            else if (k == "shadowMode") gfx.shadowMode = std::stoi(v);
            else if (k == "shadowSteps") gfx.shadowSteps = std::stoi(v);
            else if (k == "ao") gfx.ao = std::stoi(v) != 0;
            else if (k == "bounces") gfx.bounces = std::stoi(v);
            else if (k == "lightShadows") gfx.lightShadows = std::stoi(v) != 0;
            else if (k == "simplifyOnInteract") gfx.simplifyOnInteract = std::stoi(v) != 0;
            else if (k == "idleThrottle") gfx.idleThrottle = std::stoi(v) != 0;
            else if (k == "gpuIndex") gfx.gpuIndex = std::stoi(v);
            else if (k == "showGrid") editorCfg.showGrid = std::stoi(v) != 0;
            else if (k == "snap") editorCfg.snap = std::stoi(v) != 0;
            else if (k == "snapStep") editorCfg.snapStep = std::stof(v);
            else if (k == "snapAngleDeg") editorCfg.snapAngleDeg = std::stof(v);
            else if (k == "fovDeg") editorCfg.fovDeg = std::stof(v);
            else if (k == "showStats") editorCfg.showStats = std::stoi(v) != 0;
            else if (k == "projectDir") editorCfg.projectDir = v;
            else if (k == "blenderPath") editorCfg.blenderPath = v;
            else if (k == "lastVersion") editorCfg.lastVersion = v;
        } catch (...) {}
    }
    gfx.preset = std::clamp(gfx.preset, 0, (int)PRESET_CUSTOM);
    gfx.renderScale = std::clamp(gfx.renderScale, 0.2f, 1.0f);
    gfx.interactiveScale = std::clamp(gfx.interactiveScale, 0.2f, 1.0f);
    gfx.targetFps = std::clamp(gfx.targetFps, 15, 240);
    gfx.maxSteps = std::clamp(gfx.maxSteps, 16, 256);
    gfx.shadowMode = std::clamp(gfx.shadowMode, 0, (int)SHADOWS_SOFT);
    gfx.shadowSteps = std::clamp(gfx.shadowSteps, 4, 96);
    gfx.bounces = std::clamp(gfx.bounces, 1, 4);
    editorCfg.fovDeg = std::clamp(editorCfg.fovDeg, 20.0f, 110.0f);
    editorCfg.snapStep = std::max(editorCfg.snapStep, 0.01f);
    return true;
}

void saveSettings(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "# NLSDFE settings\n";
    f << "preset=" << gfx.preset << "\n";
    f << "renderScale=" << gfx.renderScale << "\n";
    f << "interactiveScale=" << gfx.interactiveScale << "\n";
    f << "autoScale=" << gfx.autoScale << "\n";
    f << "targetFps=" << gfx.targetFps << "\n";
    f << "vsync=" << gfx.vsync << "\n";
    f << "maxSteps=" << gfx.maxSteps << "\n";
    f << "shadowMode=" << gfx.shadowMode << "\n";
    f << "shadowSteps=" << gfx.shadowSteps << "\n";
    f << "ao=" << gfx.ao << "\n";
    f << "bounces=" << gfx.bounces << "\n";
    f << "lightShadows=" << gfx.lightShadows << "\n";
    f << "simplifyOnInteract=" << gfx.simplifyOnInteract << "\n";
    f << "idleThrottle=" << gfx.idleThrottle << "\n";
    f << "gpuIndex=" << gfx.gpuIndex << "\n";
    f << "showGrid=" << editorCfg.showGrid << "\n";
    f << "snap=" << editorCfg.snap << "\n";
    f << "snapStep=" << editorCfg.snapStep << "\n";
    f << "snapAngleDeg=" << editorCfg.snapAngleDeg << "\n";
    f << "fovDeg=" << editorCfg.fovDeg << "\n";
    f << "showStats=" << editorCfg.showStats << "\n";
    f << "projectDir=" << editorCfg.projectDir << "\n";
    f << "blenderPath=" << editorCfg.blenderPath << "\n";
    f << "lastVersion=" << editorCfg.lastVersion << "\n";
}
