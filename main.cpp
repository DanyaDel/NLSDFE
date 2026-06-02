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
#include "EngineExports.h"
#include "Renderer.h"

namespace fs = std::filesystem;

class Engine {
public:
    void run() { 
        #ifdef _WIN32
        CoInitialize(NULL); 
        #endif
        g_engine = this;
        
        spdlog::set_pattern("[%T] [%^%l%$] %v");
        spdlog::info("SDF Engine {} starting...", ENGINE_VERSION);

        resetScene(); 
        initWindow(); 
        renderer.init(window);
        renderer.setupInternalLogo(window);
        
        mainLoop(); 
        
        renderer.cleanup();
        cleanupWindow();

        #ifdef _WIN32
        CoUninitialize();
        #endif
        g_engine = nullptr;
        spdlog::info("SDF Engine shut down");
    }

    void PublicSaveNos(const std::string& path) {
        std::vector<uint8_t> fullRes = renderer.captureScreenshot(renderer.getSwapchainExtent(), renderer.getSwapchainImages());
        std::vector<uint8_t> thumbnail = downsamplePixels(fullRes, renderer.getSwapchainExtent().width, renderer.getSwapchainExtent().height, 256, 144);
        
        NosHeader header; strncpy(header.magic, "NOS!", 4); header.version = 100;
        header.objectCount = (uint32_t)sceneGraph.size(); header.checksum = calculateChecksum(sceneGraph);
        header.thumbnailOffset = sizeof(NosHeader) + sizeof(SceneMetadata) + (sceneGraph.size() * (sizeof(ObjectData) + sizeof(int32_t)));
        header.thumbnailSize = (uint32_t)thumbnail.size(); memset(header.reserved, 0, sizeof(header.reserved));
        
        SceneMetadata meta; meta.target = cam.target; meta.theta = cam.theta; meta.phi = cam.phi; meta.radius = cam.radius;
        memset(meta.padding, 0, sizeof(meta.padding));
        
        std::ofstream file(path, std::ios::binary); if (!file.is_open()) return;
        file.write(reinterpret_cast<const char*>(&header), sizeof(NosHeader));
        file.write(reinterpret_cast<const char*>(&meta), sizeof(SceneMetadata));
        for (const auto& node : sceneGraph) {
            file.write(reinterpret_cast<const char*>(&node.gpuData), sizeof(ObjectData));
            int32_t p = node.parent;
            file.write(reinterpret_cast<const char*>(&p), sizeof(p));
        }
        file.write(reinterpret_cast<const char*>(thumbnail.data()), thumbnail.size());
        file.close(); currentLoadedFile = path; showToast("Saved: " + path);
    }

    void PublicLoadNos(const std::string& path) {
        std::ifstream file(path, std::ios::binary); if (!file.is_open()) return;
        NosHeader header; file.read(reinterpret_cast<char*>(&header), sizeof(NosHeader));
        if (strncmp(header.magic, "NOS!", 4) != 0) return;
        SceneMetadata meta; file.read(reinterpret_cast<char*>(&meta), sizeof(SceneMetadata));
        uint32_t nodeCount = header.objectCount;
        sceneGraph.clear(); sceneGraph.reserve(nodeCount);
        for (uint32_t i = 0; i < nodeCount; ++i) {
            ObjectData od; file.read(reinterpret_cast<char*>(&od), sizeof(ObjectData));
            int32_t parent = -1; file.read(reinterpret_cast<char*>(&parent), sizeof(parent));
            SceneNode n; n.gpuData = od; n.parent = parent;
            n.localPosition = { od.position.x, od.position.y, od.position.z };
            n.localRotation = { od.rotationEuler.x, od.rotationEuler.y, od.rotationEuler.z };
            n.localScale = { od.scale.x, od.scale.y, od.scale.z };
            sceneGraph.push_back(n);
        }
        for (int i = 0; i < (int)sceneGraph.size(); ++i) {
            int p = sceneGraph[i].parent; if (p >= 0 && p < (int)sceneGraph.size()) sceneGraph[p].children.push_back(i);
        }
        cam.target = meta.target; cam.theta = meta.theta; cam.phi = meta.phi; cam.radius = meta.radius;
        selectedIndices.clear(); undoStack.clear(); currentLoadedFile = path; lastLoadedHeader = header; file.close();
    }

    void LoadMeshInternal(const std::string& path) {
        OBJModel model = parseOBJ(path);
        if (model.vertices.empty()) return;

        MeshData md;
        VkDeviceSize vSize = sizeof(Vertex) * model.vertices.size();
        renderer.createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, md.vertexBuffer.buffer, md.vertexBuffer.allocation);
        void* vData; vmaMapMemory(renderer.getAllocator(), md.vertexBuffer.allocation, &vData);
        memcpy(vData, model.vertices.data(), (size_t)vSize);
        vmaUnmapMemory(renderer.getAllocator(), md.vertexBuffer.allocation);
        md.vertexBuffer.count = (uint32_t)model.vertices.size();

        VkDeviceSize iSize = sizeof(uint32_t) * model.indices.size();
        renderer.createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, md.indexBuffer.buffer, md.indexBuffer.allocation);
        void* iData; vmaMapMemory(renderer.getAllocator(), md.indexBuffer.allocation, &iData);
        memcpy(iData, model.indices.data(), (size_t)iSize);
        vmaUnmapMemory(renderer.getAllocator(), md.indexBuffer.allocation);
        md.indexBuffer.count = (uint32_t)model.indices.size();

        md.isLoaded = true;
        meshCache.push_back(md);

        // Auto-scale and Align to Floor
        vec3 minB = {1e10,1e10,1e10}, maxB = {-1e10,-1e10,-1e10};
        for(auto& v : model.vertices) {
            minB.x = std::min(minB.x, v.pos.x); minB.y = std::min(minB.y, v.pos.y); minB.z = std::min(minB.z, v.pos.z);
            maxB.x = std::max(maxB.x, v.pos.x); maxB.y = std::max(maxB.y, v.pos.y); maxB.z = std::max(maxB.z, v.pos.z);
        }
        vec3 diag = {maxB.x-minB.x, maxB.y-minB.y, maxB.z-minB.z};
        float maxDim = std::max(diag.x, std::max(diag.y, diag.z));
        float scale = (maxDim > 0.001f) ? (2.0f / maxDim) : 1.0f;
        vec3 center = {(minB.x+maxB.x)*0.5f, minB.y, (minB.z+maxB.z)*0.5f};

        SceneNode n;
        n.name = fs::path(path).filename().string();
        n.meshIdx = (int)meshCache.size() - 1;
        n.localPosition = {-center.x * scale, -center.y * scale, -center.z * scale};
        n.localScale = {scale, scale, scale};
        n.gpuData.position = {n.localPosition.x, n.localPosition.y, n.localPosition.z, -1.0f}; 
        n.gpuData.scale = {n.localScale.x, n.localScale.y, n.localScale.z, 0};
        n.gpuData.color = {1,1,1,1};
        n.gpuData.params = {0.0f, 0.5f, 1.0f, 0}; 
        sceneGraph.push_back(n);
        showToast("Loaded model: " + n.name);
    }

    bool framebufferResized = false; 

private:
    GLFWwindow* window;
    Renderer renderer;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void initWindow() {
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); 
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
        std::vector<uint8_t> outPixels(dstW * dstH * 4);
        float xRatio = ((float)(srcW)) / dstW; float yRatio = ((float)(srcH)) / dstH;
        for (int i = 0; i < dstH; i++) {
            for (int j = 0; j < dstW; j++) {
                int px = (int)(j * xRatio); int py = (int)(i * yRatio);
                int srcIdx = (py * srcW + px) * 4; int dstIdx = (i * dstW + j) * 4;
                if (srcIdx < srcPixels.size() - 4) {
                    outPixels[dstIdx + 0] = srcPixels[srcIdx + 0]; outPixels[dstIdx + 1] = srcPixels[srcIdx + 1];
                    outPixels[dstIdx + 2] = srcPixels[srcIdx + 2]; outPixels[dstIdx + 3] = 255;
                }
            }
        }
        return outPixels;
    }

    void mainLoop() {
        int frameCount = 0; double lastTime = glfwGetTime(); double fpsTimer = lastTime;
        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime(); float deltaTime = (float)(currentTime - lastTime); lastTime = currentTime;
            frameCount++; if (currentTime - fpsTimer >= 1.0) { std::string title = "Next Level Signed Distance Field Engine | FPS: " + std::to_string(frameCount); glfwSetWindowTitle(window, title.c_str()); frameCount=0; fpsTimer=currentTime; }
            
            glfwPollEvents(); 
            processInput(window, deltaTime); 
            renderUI(window); 
            
            renderer.drawFrame(window, framebufferResized);
        }
    }
};

void* g_engine = nullptr;
void Engine_PublicSaveNos(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->PublicSaveNos(path); }
void Engine_PublicLoadNos(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->PublicLoadNos(path); }
void Engine_LoadMesh(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->LoadMeshInternal(path); }

int main() { 
    try { Engine e; e.run(); } 
    catch (const std::exception& x) { spdlog::critical("CRITICAL ERROR: {}", x.what()); std::cin.get(); return EXIT_FAILURE; } 
    return EXIT_SUCCESS; 
}
