/*
   SDF Engine - Main Source (Refactored: Renderer Extracted)
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
#include "imgui.h"

namespace fs = std::filesystem;

// Параметры командной строки
struct LaunchOptions {
    std::string openFile;      // сцена для открытия в редакторе
    std::string playFile;      // --play: запустить сцену как игру, без редактора
    std::string screenshot;    // --screenshot: сохранить SDF-кадр в .ppm и выйти
    int benchFrames = 0;       // --bench N: замерить N кадров и выйти
    int benchObjects = 0;      // --bench-objects N: добавить N объектов для нагрузки
    int shadingMode = -1;      // --mode 0..3
    int preset = -1;           // --preset 0..4
    int gpu = -2;              // --gpu N
    bool noVsync = false;
};

class Engine {
public:
    void run(const LaunchOptions& opts) {
        #ifdef _WIN32
        CoInitialize(NULL);
        #endif
        g_engine = this;
        options = opts;

        spdlog::set_pattern("[%T] [%^%l%$] %v");
        spdlog::info("SDF Engine {} starting...", ENGINE_VERSION);

        // Экспортированная игра — это тот же бинарник с game.nos по соседству
        std::string bundled = getExecutableDir() + "/game.nos";
        if (options.playFile.empty() && options.openFile.empty() && fs::exists(bundled)) options.playFile = bundled;
        bool standalone = !options.playFile.empty();
        bool automated = options.benchFrames > 0 || !options.screenshot.empty();

        bool firstRun = !loadSettings(settingsPath());
        if (options.preset >= 0) applyPreset(std::clamp(options.preset, 0, (int)PRESET_ULTRA));
        if (options.gpu >= -1) gfx.gpuIndex = options.gpu;
        if (options.noVsync || options.benchFrames > 0) gfx.vsync = false;

        resetScene();
        initWindow();
        renderer.init(window);
        renderer.setupInternalLogo(window);

        // Первый запуск: стартовое качество подбираем по типу видеокарты
        if (firstRun && options.preset < 0) {
            int preset = PRESET_POTATO;
            if (renderStats.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) preset = PRESET_HIGH;
            else if (renderStats.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) preset = PRESET_LOW;
            applyPreset(preset);
            spdlog::info("First run: quality preset '{}' selected for this GPU", presetName(preset));
            if (!automated && !standalone) saveSettings(settingsPath());
        }

        if (options.shadingMode >= 0) currentShadingMode = std::clamp(options.shadingMode, 0, 3);
        if (!options.openFile.empty()) PublicLoadNos(options.openFile, false);
        if (options.benchObjects > 0) addBenchmarkObjects(options.benchObjects);

        if (standalone) {
            if (!loadScene(options.playFile, false)) throw std::runtime_error("Cannot load scene: " + options.playFile);
            currentShadingMode = SHADING_RENDERED;
            updateSceneCaches();
            play.standalone = true;
            startPlay(window);
        }

        mainLoop();

        if (!standalone && !automated) saveSettings(settingsPath());
        renderer.cleanup();
        cleanupWindow();

        #ifdef _WIN32
        CoUninitialize();
        #endif
        g_engine = nullptr;
        spdlog::info("SDF Engine shut down");
    }

    void PublicSaveNos(const std::string& path) {
        int w = 0, h = 0;
        std::vector<uint8_t> fullRes = renderer.captureScreenshot(w, h);
        std::vector<uint8_t> thumbnail = downsamplePixels(fullRes, w, h, NOS_THUMB_W, NOS_THUMB_H);
        if (saveScene(path, thumbnail)) { currentLoadedFile = path; showToast("Saved: " + fs::path(path).filename().string()); }
        else showToast("Save failed: " + path);
    }

    void PublicLoadNos(const std::string& path, bool append) {
        if (!loadScene(path, append)) { showToast("Cannot load: " + fs::path(path).filename().string()); return; }
        showToast((append ? "Appended: " : "Loaded: ") + fs::path(path).filename().string());
    }

    // Меши кэшируются по пути: один файл — один набор буферов на GPU
    int GetOrLoadMesh(const std::string& path) {
        for (int i = 0; i < (int)meshCache.size(); i++) if (meshCache[i].path == path) return meshCache[i].isLoaded ? i : -1;
        OBJModel model = parseOBJ(path);
        MeshData md;
        md.path = path;
        if (!renderer.uploadMesh(model, md)) { spdlog::error("Cannot load mesh: {}", path); return -1; }
        meshCache.push_back(md);
        return (int)meshCache.size() - 1;
    }

    void LoadMeshInternal(const std::string& path) {
        if (sceneGraph.size() >= MAX_OBJECTS) { showToast("Scene is full"); return; }
        int meshIdx = GetOrLoadMesh(path);
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

    // Игра = бинарник движка + шейдеры + сцена game.nos в одной папке
    bool ExportGame(const std::string& directory) {
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (!fs::is_directory(directory, ec)) return false;

        fs::path exe = getExecutablePath();
        fs::copy_file(exe, fs::path(directory) / exe.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) { spdlog::error("Export: cannot copy executable: {}", ec.message()); return false; }
        for (const auto& entry : fs::directory_iterator(getExecutableDir(), ec)) {
            if (entry.path().extension() == ".spv") fs::copy_file(entry.path(), fs::path(directory) / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
        // Меши кладём рядом со сценой: загрузчик ищет их там, если исходного пути нет
        for (const auto& node : sceneGraph) {
            if (node.meshPath.empty()) continue;
            fs::copy_file(node.meshPath, fs::path(directory) / fs::path(node.meshPath).filename(), fs::copy_options::overwrite_existing, ec);
        }
        int w = 0, h = 0;
        std::vector<uint8_t> shot = renderer.captureScreenshot(w, h);
        if (!saveScene((fs::path(directory) / "game.nos").string(), downsamplePixels(shot, w, h, NOS_THUMB_W, NOS_THUMB_H))) return false;
        saveSettings((fs::path(directory) / settingsPath()).string());
        spdlog::info("Game exported to {}", directory);
        return true;
    }

    bool framebufferResized = false;

private:
    GLFWwindow* window;
    Renderer renderer;
    LaunchOptions options;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void initWindow() {
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan is not supported on this system: install a Vulkan driver for your GPU");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // Замеры идут в окне фиксированного размера, иначе тайловый WM растянет его как захочет
        if (options.benchFrames > 0 || !options.screenshot.empty()) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(INITIAL_WIDTH, INITIAL_HEIGHT, "Next Level Signed Distance Field Engine", nullptr, nullptr);
        if (!window) throw std::runtime_error("Failed to create window");
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetCursorPosCallback(window, cursor_pos_callback);
        glfwSetMouseButtonCallback(window, mouse_button_callback);
        glfwSetScrollCallback(window, scroll_callback);
    }

    void cleanupWindow() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    std::vector<uint8_t> downsamplePixels(const std::vector<uint8_t>& srcPixels, int srcW, int srcH, int dstW, int dstH) {
        std::vector<uint8_t> outPixels((size_t)dstW * dstH * 4, 0);
        if (srcW <= 0 || srcH <= 0 || srcPixels.size() < (size_t)srcW * srcH * 4) return outPixels;
        float xRatio = ((float)(srcW)) / dstW; float yRatio = ((float)(srcH)) / dstH;
        for (int i = 0; i < dstH; i++) {
            for (int j = 0; j < dstW; j++) {
                int px = std::min((int)(j * xRatio), srcW - 1); int py = std::min((int)(i * yRatio), srcH - 1);
                int srcIdx = (py * srcW + px) * 4; int dstIdx = (i * dstW + j) * 4;
                outPixels[dstIdx + 0] = srcPixels[srcIdx + 0]; outPixels[dstIdx + 1] = srcPixels[srcIdx + 1];
                outPixels[dstIdx + 2] = srcPixels[srcIdx + 2]; outPixels[dstIdx + 3] = 255;
            }
        }
        return outPixels;
    }

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

    void writeScreenshot(const std::string& path) {
        int w = 0, h = 0;
        std::vector<uint8_t> pixels = renderer.captureScreenshot(w, h);
        if (pixels.empty()) { spdlog::error("Screenshot failed: nothing rendered yet"); return; }
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << w << " " << h << "\n255\n";
        for (int i = 0; i < w * h; i++) f.write(reinterpret_cast<const char*>(&pixels[i * 4]), 3);
        spdlog::info("Screenshot saved: {} ({}x{})", path, w, h);
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
        if (isPlayMode()) { play.standalone = false; stopPlay(window); }
    }
};

void* g_engine = nullptr;
static Engine* engine() { return reinterpret_cast<Engine*>(g_engine); }
void Engine_PublicSaveNos(const std::string& path) { if (g_engine) engine()->PublicSaveNos(path); }
void Engine_PublicLoadNos(const std::string& path, bool append) { if (g_engine) engine()->PublicLoadNos(path, append); }
void Engine_LoadMesh(const std::string& path) { if (g_engine) engine()->LoadMeshInternal(path); }
int Engine_GetOrLoadMesh(const std::string& path) { return g_engine ? engine()->GetOrLoadMesh(path) : -1; }
void* Engine_CreateUiTexture(const uint8_t* rgba, int w, int h) { return g_engine ? engine()->CreateUiTexture(rgba, w, h) : nullptr; }
void Engine_DestroyUiTexture(void* textureId) { if (g_engine) engine()->DestroyUiTexture(textureId); }
bool Engine_ExportGame(const std::string& directory) { return g_engine && engine()->ExportGame(directory); }

static void printUsage() {
    std::cout <<
        "Usage: SDF_Engine [scene.nos] [options]\n"
        "  --play <scene.nos>     run the scene as a game (no editor)\n"
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
            else if (a == "--play") opts.playFile = next();
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
