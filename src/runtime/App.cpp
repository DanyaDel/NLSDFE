#include "App.h"
#include "Scene.h"
#include "Settings.h"
#include "Audio.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace {
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
}

void App::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void App::initApp(const char* title, bool fixedSize) {
    if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
    if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan is not supported on this system: install a Vulkan driver for your GPU");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (fixedSize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(INITIAL_WIDTH, INITIAL_HEIGHT, title, nullptr, nullptr);
    if (!window) throw std::runtime_error("Failed to create window");
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    renderer.init(window);
    renderer.setupInternalLogo(window);
    g_meshLoader = [this](const std::string& path) { return getOrLoadMesh(path); };
}

void App::shutdownApp() {
    g_meshLoader = nullptr;
    audio::shutdown();
    renderer.cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void App::applyFirstRunPreset() {
    int preset = PRESET_POTATO;
    if (renderStats.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) preset = PRESET_HIGH;
    else if (renderStats.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) preset = PRESET_LOW;
    applyPreset(preset);
    spdlog::info("First run: quality preset '{}' selected for this GPU", presetName(preset));
}

int App::getOrLoadMesh(const std::string& path) {
    for (int i = 0; i < (int)meshCache.size(); i++) if (meshCache[i].path == path) return meshCache[i].isLoaded ? i : -1;
    OBJModel model = parseOBJ(path);
    MeshData md;
    md.path = path;
    if (!renderer.uploadMesh(model, md)) { spdlog::error("Cannot load mesh: {}", path); return -1; }
    meshCache.push_back(md);
    return (int)meshCache.size() - 1;
}

std::vector<uint8_t> App::captureThumbnail() {
    int w = 0, h = 0;
    std::vector<uint8_t> fullRes = renderer.captureScreenshot(w, h);
    return downsamplePixels(fullRes, w, h, NOS_THUMB_W, NOS_THUMB_H);
}

void App::writeScreenshot(const std::string& path) {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels = renderer.captureScreenshot(w, h);
    if (pixels.empty()) { spdlog::error("Screenshot failed: nothing rendered yet"); return; }
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; i++) f.write(reinterpret_cast<const char*>(&pixels[i * 4]), 3);
    spdlog::info("Screenshot saved: {} ({}x{})", path, w, h);
}
