/*
   SDF Engine - Editor entry point
*/

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #define GLFW_EXPOSE_NATIVE_WIN32
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#ifdef _WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <iostream>
#include <filesystem>
#include <string>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <vector>

#include "Common.h"
#include "MathUtils.h"
#include "EditorUI.h"
#include "EngineBridge.h"
#include "Scene.h"
#include "Input.h"
#include "Settings.h"
#include "Game.h"
#include "EngineExports.h"
#include "Renderer.h"
#include "App.h"
#include "imgui.h"

namespace fs = std::filesystem;

// Параметры командной строки
struct LaunchOptions {
    std::string openFile;      // сцена для открытия в редакторе
    std::string screenshot;    // --screenshot: сохранить SDF-кадр в .ppm и выйти
    int benchFrames = 0;       // --bench N: замерить N кадров и выйти
    int benchObjects = 0;      // --bench-objects N: добавить N объектов для нагрузки
    int shadingMode = -1;      // --mode 0..3
    int preset = -1;           // --preset 0..4
    int gpu = -2;              // --gpu N
    bool noVsync = false;
};

class Engine : public App {
public:
    void run(const LaunchOptions& opts) {
        #ifdef _WIN32
        CoInitialize(NULL);
        #endif
        g_engine = this;
        options = opts;

        spdlog::set_pattern("[%T] [%^%l%$] %v");
        spdlog::info("SDF Engine {} starting...", ENGINE_VERSION);

        bool automated = options.benchFrames > 0 || !options.screenshot.empty();

        bool firstRun = !loadSettings(settingsPath());
        if (options.preset >= 0) applyPreset(std::clamp(options.preset, 0, (int)PRESET_ULTRA));
        if (options.gpu >= -1) gfx.gpuIndex = options.gpu;
        if (options.noVsync || options.benchFrames > 0) gfx.vsync = false;

        resetScene();
        initApp("Next Level Signed Distance Field Engine", automated);
        glfwSetCursorPosCallback(window, cursor_pos_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetScrollCallback(window, scroll_callback);

        if (firstRun && options.preset < 0) {
            applyFirstRunPreset();
            if (!automated) saveSettings(settingsPath());
        }

        if (options.shadingMode >= 0) currentShadingMode = std::clamp(options.shadingMode, 0, 3);
        if (!options.openFile.empty()) PublicLoadNos(options.openFile, false);
        if (options.benchObjects > 0) addBenchmarkObjects(options.benchObjects);

        mainLoop();

        if (!automated) saveSettings(settingsPath());
        shutdownApp();

        #ifdef _WIN32
        CoUninitialize();
        #endif
        g_engine = nullptr;
        spdlog::info("SDF Engine shut down");
    }

    void PublicSaveNos(const std::string& path) {
        if (saveScene(path, captureThumbnail())) { currentLoadedFile = path; showToast("Saved: " + fs::path(path).filename().string()); }
        else showToast("Save failed: " + path);
    }

    void PublicLoadNos(const std::string& path, bool append) {
        if (!loadScene(path, append)) { showToast("Cannot load: " + fs::path(path).filename().string()); return; }
        showToast((append ? "Appended: " : "Loaded: ") + fs::path(path).filename().string());
    }

    void LoadMeshInternal(const std::string& path) {
        if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); return; }
        int meshIdx = getOrLoadMesh(path);
        if (meshIdx < 0) { showToast("Cannot load mesh"); return; }
        const MeshData& md = meshCache[meshIdx];

        // Auto-scale and Align to Floor
        vec3 diag = md.boundsMax - md.boundsMin;
        float maxDim = std::max(diag.x, std::max(diag.y, diag.z));
        float scale = (maxDim > 0.001f) ? (2.0f / maxDim) : 1.0f;
        vec3 center = {(md.boundsMin.x + md.boundsMax.x) * 0.5f, md.boundsMin.y, (md.boundsMin.z + md.boundsMax.z) * 0.5f};

        saveUndo();
        SceneNode n;
        n.name = fs::path(path).filename().string();
        n.meshIdx = meshIdx;
        n.meshPath = path;
        n.localPosition = -center * scale;
        n.localScale = {scale, scale, scale};
        n.gpuData.color = {1,1,1,1};
        n.gpuData.params = {0.0f, 0.5f, 1.0f, 0};
        sceneGraph.push_back(n);
        selectedIndices = { (int)sceneGraph.size() - 1 };
        showToast("Loaded model: " + n.name);
    }

    void* CreateUiTexture(const uint8_t* rgba, int w, int h) { return renderer.createTexture(rgba, w, h); }
    void DestroyUiTexture(void* id) { renderer.destroyTexture(id); }

    // Игра = плеер + шейдеры + сцена game.nos в одной папке
    bool ExportGame(const std::string& directory) {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (!fs::is_directory(directory, ec)) return false;

        // В игру уходит плеер, а не редактор
        fs::path exe = getExecutablePath();
        fs::path player = exe.parent_path() / ("NLSDFE_Player" + exe.extension().string());
        if (!fs::exists(player, ec)) { spdlog::error("Export: {} not found next to the editor", player.string()); return false; }
        fs::copy_file(player, fs::path(directory) / player.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) { spdlog::error("Export: cannot copy player: {}", ec.message()); return false; }
        for (const auto& entry : fs::directory_iterator(getExecutableDir(), ec)) {
            if (entry.path().extension() == ".spv") fs::copy_file(entry.path(), fs::path(directory) / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
        // Меши кладём рядом со сценой: загрузчик ищет их там, если исходного пути нет
        for (const auto& node : sceneGraph) {
            if (node.meshPath.empty()) continue;
            fs::copy_file(node.meshPath, fs::path(directory) / fs::path(node.meshPath).filename(), fs::copy_options::overwrite_existing, ec);
        }
        if (!saveScene((fs::path(directory) / "game.nos").string(), captureThumbnail())) return false;
        saveSettings((fs::path(directory) / settingsPath()).string());
        spdlog::info("Game exported to {}", directory);
        return true;
    }

private:
    LaunchOptions options;

    // Сетка из чередующихся примитивов — нагрузка для --bench
    void addBenchmarkObjects(int count) {
        int side = (int)std::ceil(std::sqrt((float)count));
        for (int i = 0; i < count && sceneGraph.size() < MAX_OBJECTS; i++) {
            SceneNode n;
            n.name = "Bench " + std::to_string(i);
            n.localPosition = { (i % side - side * 0.5f) * 1.2f, 0.4f, (i / side - side * 0.5f) * 1.2f };
            n.localScale = vec3(0.4f);
            n.gpuData.position.w = (float)(i % 2);   // Только сфера и куб: сцена совпадает с замерами старых версий
            n.gpuData.params = vec4(1.0f, 0, 0.1f, (i % 3 == 0) ? 1.0f : 0.0f);
            n.gpuData.color = vec4(0.3f + 0.7f * ((i * 37) % 10) / 10.0f, 0.3f + 0.7f * ((i * 53) % 10) / 10.0f, 0.3f + 0.7f * ((i * 71) % 10) / 10.0f, 0.3f);
            sceneGraph.push_back(n);
        }
        cam.radius = 12.0f;
    }

    void mainLoop() {
        int frameCount = 0; double lastTime = glfwGetTime(); double fpsTimer = lastTime;
        int quietFrames = 0;
        uint64_t totalFrames = 0;
        const int warmupFrames = 30;
        double benchStart = 0.0;
        bool automated = options.benchFrames > 0 || !options.screenshot.empty();
        renderer.continuous = options.benchFrames > 0;

        while (!glfwWindowShouldClose(window)) {
            // Пока ничего не происходит, спим до следующего события вместо молотьбы кадров
            bool idle = gfx.idleThrottle && !automated && !isPlayMode() && !uiNeedsAnimation() && quietFrames > 5;
            if (idle) glfwWaitEventsTimeout(0.1); else glfwPollEvents();

            double currentTime = glfwGetTime(); float deltaTime = (float)(currentTime - lastTime); lastTime = currentTime;
            frameCount++; if (currentTime - fpsTimer >= 1.0) { std::string title = "Next Level Signed Distance Field Engine | FPS: " + std::to_string(frameCount); glfwSetWindowTitle(window, title.c_str()); frameCount=0; fpsTimer=currentTime; }

            updateSceneCaches();
            beginUIFrame();
            processInput(window, deltaTime);
            updatePlay(window, deltaTime);
            renderUI(window);
            renderer.drawGrid = editorCfg.showGrid && !isPlayMode();
            updateSceneCaches();   // Правки этого кадра (гизмо, инспектор, логика) сразу попадают в рендер

            renderer.drawFrame(window, framebufferResized);

            ImGuiIO& io = ImGui::GetIO();
            bool busy = renderStats.renderedThisFrame || ImGui::IsAnyItemActive() || ImGui::IsAnyMouseDown() || io.MouseWheel != 0.0f
                     || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.InputQueueCharacters.Size > 0;
            quietFrames = busy ? 0 : quietFrames + 1;

            totalFrames++;
            if (automated) {
                if (totalFrames == (uint64_t)warmupFrames) {
                    benchStart = glfwGetTime();
                    renderStats.gpuMsSum = 0.0; renderStats.gpuMsCount = 0;
                }
                if (totalFrames >= (uint64_t)(warmupFrames + options.benchFrames)) {
                    if (options.benchFrames > 0) {
                        double avgMs = (glfwGetTime() - benchStart) * 1000.0 / options.benchFrames;
                        double gpuMs = renderStats.gpuMsCount ? renderStats.gpuMsSum / renderStats.gpuMsCount : 0.0;
                        spdlog::info("BENCH frames={} objects={} res={}x{} mode={} preset={} avg_frame_ms={:.3f} fps={:.1f} gpu_compute_ms={:.3f}",
                                     options.benchFrames, gpuObjectCache.size(), renderStats.renderW, renderStats.renderH,
                                     currentShadingMode, presetName(gfx.preset), avgMs, 1000.0 / avgMs, gpuMs);
                    }
                    if (!options.screenshot.empty()) writeScreenshot(options.screenshot);
                    break;
                }
            }
        }
        if (renderStats.validationErrors > 0) spdlog::error("Vulkan validation errors: {}", renderStats.validationErrors);
        if (isPlayMode()) stopPlay(window);
    }
};

void* g_engine = nullptr;
static Engine* engine() { return reinterpret_cast<Engine*>(g_engine); }
void Engine_PublicSaveNos(const std::string& path) { if (g_engine) engine()->PublicSaveNos(path); }
void Engine_PublicLoadNos(const std::string& path, bool append) { if (g_engine) engine()->PublicLoadNos(path, append); }
void Engine_LoadMesh(const std::string& path) { if (g_engine) engine()->LoadMeshInternal(path); }
void* Engine_CreateUiTexture(const uint8_t* rgba, int w, int h) { return g_engine ? engine()->CreateUiTexture(rgba, w, h) : nullptr; }
void Engine_DestroyUiTexture(void* textureId) { if (g_engine) engine()->DestroyUiTexture(textureId); }
bool Engine_ExportGame(const std::string& directory) { return g_engine && engine()->ExportGame(directory); }

static void printUsage() {
    std::cout <<
        "Usage: SDF_Engine [scene.nos] [options]\n"
        "  --preset <0..4>        quality preset: 0 Potato, 1 Low, 2 Medium, 3 High, 4 Ultra\n"
        "  --mode <0..3>          shading: 0 Wire, 1 Solid, 2 Material, 3 Rendered\n"
        "  --gpu <index>          pick a GPU (-1 = auto)\n"
        "  --no-vsync             uncapped frame rate\n"
        "  --bench <frames>       measure average frame time and exit\n"
        "  --bench-objects <n>    add n objects to the scene for --bench\n"
        "  --screenshot <out.ppm> save the SDF frame and exit\n"
        "Environment: NLSDFE_VALIDATION=1 enables the Vulkan validation layer.\n";
}

int main(int argc, char** argv) {
    LaunchOptions opts;
    try {
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::runtime_error("Missing value for " + a); return argv[++i]; };
            if (a == "--help" || a == "-h") { printUsage(); return EXIT_SUCCESS; }
            else if (a == "--preset") opts.preset = std::stoi(next());
            else if (a == "--mode") opts.shadingMode = std::stoi(next());
            else if (a == "--gpu") opts.gpu = std::stoi(next());
            else if (a == "--no-vsync") opts.noVsync = true;
            else if (a == "--bench") opts.benchFrames = std::stoi(next());
            else if (a == "--bench-objects") opts.benchObjects = std::stoi(next());
            else if (a == "--screenshot") opts.screenshot = next();
            else if (!a.empty() && a[0] != '-') opts.openFile = a;
            else throw std::runtime_error("Unknown option " + a);
        }
    } catch (const std::exception& x) { std::cerr << x.what() << "\n"; printUsage(); return EXIT_FAILURE; }

    try { Engine e; e.run(opts); }
    catch (const std::exception& x) {
        spdlog::critical("CRITICAL ERROR: {}", x.what());
        #ifdef _WIN32
        std::cin.get();   // Иначе консоль закроется раньше, чем удастся прочитать ошибку
        #endif
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
