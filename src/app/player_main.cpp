/*
   NLSDFE Player - движок без редактора: загружает сцену и запускает игру
*/

#include <iostream>
#include <filesystem>
#include <string>
#include <algorithm>

#include "Common.h"
#include "Scene.h"
#include "Settings.h"
#include "Game.h"
#include "Renderer.h"
#include "App.h"
#include "imgui.h"

namespace fs = std::filesystem;

struct PlayerOptions {
    std::string scene;         // по умолчанию game.nos рядом с исполняемым файлом
    std::string screenshot;    // --screenshot: сохранить SDF-кадр и выйти
    int frames = 0;            // --frames N: выйти после N кадров
    int preset = -1;
    int gpu = -2;
    bool noVsync = false;
};

class Player : public App {
public:
    void run(const PlayerOptions& opts) {
        spdlog::set_pattern("[%T] [%^%l%$] %v");
        spdlog::info("NLSDFE Player {} starting...", ENGINE_VERSION);

        std::string scene = opts.scene.empty() ? getExecutableDir() + "/game.nos" : opts.scene;
        if (!fs::exists(scene)) throw std::runtime_error("Scene not found: " + scene);

        // Настройки игры лежат рядом с ней, а не в рабочей папке
        bool firstRun = !loadSettings(getExecutableDir() + "/" + settingsPath());
        if (opts.preset >= 0) applyPreset(std::clamp(opts.preset, 0, (int)PRESET_ULTRA));
        if (opts.gpu >= -1) gfx.gpuIndex = opts.gpu;
        if (opts.noVsync) gfx.vsync = false;

        bool automated = opts.frames > 0 || !opts.screenshot.empty();
        initApp("NLSDFE Player", automated);
        if (firstRun && opts.preset < 0) applyFirstRunPreset();
        ImGui::GetIO().IniFilename = nullptr;   // Игре незачем писать imgui.ini

        if (!loadScene(scene, false)) throw std::runtime_error("Cannot load scene: " + scene);
        glfwSetWindowTitle(window, fs::path(scene).stem().string().c_str());
        currentShadingMode = SHADING_RENDERED;
        renderer.drawGrid = false;
        updateSceneCaches();
        play.standalone = true;
        startPlay(window);

        int frame = 0;
        int lastFrame = opts.frames > 0 ? opts.frames : (opts.screenshot.empty() ? 0 : 30);
        double lastTime = glfwGetTime();
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            double now = glfwGetTime(); float deltaTime = (float)(now - lastTime); lastTime = now;
            // Автоматический прогон идёт с фиксированным шагом, чтобы результат повторялся
            if (automated) deltaTime = 1.0f / 60.0f;

            updateSceneCaches();
            uiNewFrame();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) stopPlay(window);
            updatePlay(window, deltaTime);
            drawPlayHud();
            ImGui::Render();
            updateSceneCaches();
            renderer.drawFrame(window, framebufferResized);

            if (lastFrame > 0 && ++frame >= lastFrame) {
                if (!opts.screenshot.empty()) writeScreenshot(opts.screenshot);
                break;
            }
        }
        if (renderStats.validationErrors > 0) spdlog::error("Vulkan validation errors: {}", renderStats.validationErrors);
        shutdownApp();
        spdlog::info("NLSDFE Player shut down");
    }
};

static void printUsage() {
    std::cout <<
        "Usage: NLSDFE_Player [scene.nos] [options]\n"
        "  Without arguments plays game.nos located next to the executable.\n"
        "  --preset <0..4>        quality preset: 0 Potato, 1 Low, 2 Medium, 3 High, 4 Ultra\n"
        "  --gpu <index>          pick a GPU (-1 = auto)\n"
        "  --no-vsync             uncapped frame rate\n"
        "  --frames <n>           run n frames with a fixed time step and exit\n"
        "  --screenshot <out.ppm> save the SDF frame and exit\n";
}

int main(int argc, char** argv) {
    PlayerOptions opts;
    try {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::runtime_error("Missing value for " + a); return argv[++i]; };
            if (a == "--help" || a == "-h") { printUsage(); return EXIT_SUCCESS; }
            else if (a == "--preset") opts.preset = std::stoi(next());
            else if (a == "--gpu") opts.gpu = std::stoi(next());
            else if (a == "--no-vsync") opts.noVsync = true;
            else if (a == "--frames") opts.frames = std::stoi(next());
            else if (a == "--screenshot") opts.screenshot = next();
            else if (!a.empty() && a[0] != '-') opts.scene = a;
            else throw std::runtime_error("Unknown option " + a);
        }
    } catch (const std::exception& x) { std::cerr << x.what() << "\n"; printUsage(); return EXIT_FAILURE; }

    try { Player p; p.run(opts); }
    catch (const std::exception& x) { spdlog::critical("CRITICAL ERROR: {}", x.what()); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
