/*
   SDF Engine - Main Source (Fix: Gizmo Position for Children)
*/

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <commdlg.h>
    #include <shlobj.h>
    #define GLFW_EXPOSE_NATIVE_WIN32
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#ifdef _WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cstring>
#include <array>
#include <numeric>

#include "Common.h"
#include "MathUtils.h"
#include "EditorUI.h"
#include "EngineBridge.h"
#include "Scene.h"
#include "Input.h"
#include "EngineExports.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace fs = std::filesystem;

void updateCameraMatrix(float* m) {
    CameraBasis b = getCameraBasis();
    m[0] = b.right.x; m[1] = b.right.y; m[2] = b.right.z; m[3] = 0.0f;
    m[4] = b.up.x;    m[5] = b.up.y;    m[6] = b.up.z;    m[7] = 0.0f;
    m[8] = -b.fwd.x;  m[9] = -b.fwd.y;  m[10]= -b.fwd.z;  m[11]= 0.0f;
    m[12]= b.pos.x;   m[13]= b.pos.y;   m[14]= b.pos.z;   m[15]= 1.0f;
}

class Engine {
public:
    void run() { 
        #ifdef _WIN32
        CoInitialize(NULL); 
        #endif
        g_engine = this;
        resetScene(); 
        initWindow(); 
        initVulkan(); 
        initImGui(); 
        setupInternalLogo();
        mainLoop(); 
        cleanup(); 
        #ifdef _WIN32
        CoUninitialize();
        #endif
        g_engine = nullptr;
    }

    void PublicSaveNos(const std::string& path) { saveNosInternal(path); }
    void PublicLoadNos(const std::string& path) { loadNosInternal(path); }
    void LoadMeshInternal(const std::string& path) {
        OBJModel model = parseOBJ(path);
        if (model.vertices.empty()) return;

        MeshData md;
        VkDeviceSize vSize = sizeof(Vertex) * model.vertices.size();
        createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, md.vertexBuffer.buffer, md.vertexBuffer.memory);
        void* vData; vkMapMemory(device, md.vertexBuffer.memory, 0, vSize, 0, &vData);
        memcpy(vData, model.vertices.data(), (size_t)vSize);
        vkUnmapMemory(device, md.vertexBuffer.memory);
        md.vertexBuffer.count = (uint32_t)model.vertices.size();

        VkDeviceSize iSize = sizeof(uint32_t) * model.indices.size();
        createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, md.indexBuffer.buffer, md.indexBuffer.memory);
        void* iData; vkMapMemory(device, md.indexBuffer.memory, 0, iSize, 0, &iData);
        memcpy(iData, model.indices.data(), (size_t)iSize);
        vkUnmapMemory(device, md.indexBuffer.memory);
        md.indexBuffer.count = (uint32_t)model.indices.size();

        md.isLoaded = true;
        meshCache.push_back(md);

        SceneNode n;
        n.name = fs::path(path).filename().string();
        n.meshIdx = (int)meshCache.size() - 1;
        n.gpuData.position = {0,0,0, -1.0f}; 
        n.gpuData.scale = {1,1,1,0};
        n.gpuData.color = {1,1,1,1};
        n.gpuData.params = {0.0f, 0.5f, 1.0f, 0}; // metallic, roughness, ao
        sceneGraph.push_back(n);
        showToast("Loaded model: " + n.name);
    }

    bool framebufferResized = false; 

private:
    GLFWwindow* window;
    VkInstance instance; VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; VkDevice device;
    VkQueue computeQueue, presentQueue; VkSurfaceKHR surface; VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages; std::vector<VkImageView> swapchainImageViews;
    VkExtent2D swapchainExtent;
    VkPipelineLayout pipelineLayout; VkPipeline computePipeline;
    VkPipelineLayout graphicsPipelineLayout; VkPipeline graphicsPipeline;
    VkDescriptorSetLayout descriptorSetLayout; VkDescriptorPool descriptorPool; std::vector<VkDescriptorSet> descriptorSets;
    VkImage depthImage; VkDeviceMemory depthImageMemory; VkImageView depthImageView;
    VkCommandPool commandPool; std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable, renderFinished; std::vector<VkFence> inFlightFences;
    fs::file_time_type lastShaderWriteTime;
    VkBuffer sceneBuffer; VkDeviceMemory sceneBufferMemory; void* sceneBufferMapped = nullptr;

    VkDescriptorPool imguiDescriptorPool;
    VkRenderPass imguiRenderPass;
    std::vector<VkFramebuffer> imguiFramebuffers;
    vec4 currentUiBounds = {0,0,0,0}; 

    VkImage logoImage = VK_NULL_HANDLE;
    VkDeviceMemory logoImageMemory = VK_NULL_HANDLE;
    VkImageView logoImageView = VK_NULL_HANDLE;
    VkSampler logoSampler = VK_NULL_HANDLE;

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

     std::vector<unsigned char> generateLogoPixels(int width, int height) {
        std::vector<unsigned char> pixels(width * height * 4, 0);
         auto drawRect = [&](int x, int y, int w, int h) {
            for(int i = y; i < y + h; i++) {
                for(int j = x; j < x + w; j++) {
                    if (j >= 0 && j < width && i >= 0 && i < height) {
                        int idx = (i * width + j) * 4;
                        pixels[idx + 0] = 130; pixels[idx + 1] = 130; pixels[idx + 2] = 130; pixels[idx + 3] = 255; 
                    }
                }
            }
        };
        float t = 4.8; float gap = 4.8; 
        int a1_x = 10; int a1_y = 10; int a1_w = 44; int a1_h = 44;
        drawRect(a1_x, a1_y, t, a1_h); drawRect(a1_x + a1_w - t, a1_y, t, a1_h); drawRect(a1_x, a1_y, a1_w, t);
        int a2_x = a1_x + t + gap; int a2_y = a1_y + t + gap; int a2_w = a1_w - (t + gap) * 2; int a2_h = a1_h - (t + gap);
        drawRect(a2_x, a2_y, t, a2_h); drawRect(a2_x + a2_w - t, a2_y, t, a2_h); drawRect(a2_x, a2_y, a2_w, t);
        return pixels;
    }

    void setWindowIconInternal(const std::vector<unsigned char>& pixels, int w, int h) {
        GLFWimage images[1]; images[0].width = w; images[0].height = h; images[0].pixels = const_cast<unsigned char*>(pixels.data()); glfwSetWindowIcon(window, 1, images); 
    }

    void createLogoTexture(const std::vector<unsigned char>& pixels, int w, int h) {
        VkDeviceSize imageSize = w * h * 4;
        VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        void* data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels.data(), static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory);
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent.width = w; imageInfo.extent.height = h; imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        vkCreateImage(device, &imageInfo, nullptr, &logoImage);
        VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, logoImage, &memReqs);
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &logoImageMemory); vkBindImageMemory(device, logoImage, logoImageMemory, 0);
        VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cmdAlloc.commandPool = commandPool; cmdAlloc.commandBufferCount = 1;
        VkCommandBuffer cb; vkAllocateCommandBuffers(device, &cmdAlloc, &cb);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &beginInfo);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = logoImage; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cb, stagingBuffer, logoImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(cb); VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submitInfo.commandBufferCount = 1; submitInfo.pCommandBuffers = &cb;
        vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE); vkQueueWaitIdle(computeQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cb); vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = logoImage; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &viewInfo, nullptr, &logoImageView);
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR; samplerInfo.minFilter = VK_FILTER_LINEAR; samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        vkCreateSampler(device, &samplerInfo, nullptr, &logoSampler);
        g_AppLogoID = ImGui_ImplVulkan_AddTexture(logoSampler, logoImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void setupInternalLogo() {
        int w = 64, h = 64; std::vector<unsigned char> pixels = generateLogoPixels(w, h);
        setWindowIconInternal(pixels, w, h); createLogoTexture(pixels, w, h);
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
    
    uint32_t findMemoryType(uint32_t f, VkMemoryPropertyFlags p) { VkPhysicalDeviceMemoryProperties m; vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m); for(uint32_t i=0; i<m.memoryTypeCount; i++) if((f&(1<<i))&&(m.memoryTypes[i].propertyFlags&p)==p) return i; throw std::runtime_error("mem"); }
    void createBuffer(VkDeviceSize s, VkBufferUsageFlags u, VkMemoryPropertyFlags p, VkBuffer& b, VkDeviceMemory& m) { VkBufferCreateInfo i{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; i.size=s; i.usage=u; vkCreateBuffer(device,&i,nullptr,&b); VkMemoryRequirements r; vkGetBufferMemoryRequirements(device,b,&r); VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=r.size; a.memoryTypeIndex=findMemoryType(r.memoryTypeBits,p); vkAllocateMemory(device,&a,nullptr,&m); vkBindBufferMemory(device,b,m,0); }
    
    std::vector<uint8_t> captureScreenshot() {
        vkDeviceWaitIdle(device);
        VkDeviceSize imageSize = swapchainExtent.width * swapchainExtent.height * 4;
        VkBuffer dstBuffer; VkDeviceMemory dstBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, dstBuffer, dstBufferMemory);
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocInfo.commandPool = commandPool; allocInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer; vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImages[0]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = {swapchainExtent.width, swapchainExtent.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, swapchainImages[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(commandBuffer);
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submitInfo.commandBufferCount = 1; submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE); vkQueueWaitIdle(computeQueue);
        std::vector<uint8_t> pixels(imageSize); void* data; vkMapMemory(device, dstBufferMemory, 0, imageSize, 0, &data);
        memcpy(pixels.data(), data, (size_t)imageSize); vkUnmapMemory(device, dstBufferMemory);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer); vkDestroyBuffer(device, dstBuffer, nullptr); vkFreeMemory(device, dstBufferMemory, nullptr);
        for(size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i+2]); 
        return pixels;
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

    void saveNosInternal(const std::string& path) {
        std::vector<uint8_t> fullRes = captureScreenshot();
        std::vector<uint8_t> thumbnail = downsamplePixels(fullRes, swapchainExtent.width, swapchainExtent.height, 256, 144);
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

    void loadNosInternal(const std::string& path) {
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

    void createSSBO() { createBuffer(sizeof(ObjectData)*MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sceneBuffer, sceneBufferMemory); vkMapMemory(device, sceneBufferMemory, 0, sizeof(ObjectData)*MAX_OBJECTS, 0, &sceneBufferMapped); }
    
    void updateSSBO() {
        gpuObjectCache.clear();
        mat4 identity; memset(identity.m, 0, sizeof(float)*16); identity.m[0]=1; identity.m[5]=1; identity.m[10]=1; identity.m[15]=1;
        for (int i = 0; i < sceneGraph.size(); i++) {
            if (sceneGraph[i].parent == -1) {
                updateHierarchy(i, identity);
            }
        }
        for (const auto& node : sceneGraph) {
            gpuObjectCache.push_back(node.gpuData);
            if (gpuObjectCache.size() >= MAX_OBJECTS) break;
        }
        if (!gpuObjectCache.empty()) {
            memcpy(sceneBufferMapped, gpuObjectCache.data(), gpuObjectCache.size() * sizeof(ObjectData));
        }
    }
    
    mat4 calculateViewMatrix() {
        CameraBasis b = getCameraBasis();
        mat4 v = identityMatrix();
        v.m[0] = b.right.x; v.m[4] = b.right.y; v.m[8] = b.right.z;
        v.m[1] = b.up.x;    v.m[5] = b.up.y;    v.m[9] = b.up.z;
        v.m[2] = -b.fwd.x;  v.m[6] = -b.fwd.y;  v.m[10]= -b.fwd.z;
        v.m[12] = -(v.m[0] * b.pos.x + v.m[4] * b.pos.y + v.m[8] * b.pos.z);
        v.m[13] = -(v.m[1] * b.pos.x + v.m[5] * b.pos.y + v.m[9] * b.pos.z);
        v.m[14] = -(v.m[2] * b.pos.x + v.m[6] * b.pos.y + v.m[10]* b.pos.z);
        return v;
    }

    mat4 calculateProjectionMatrix() {
        float fov = 45.0f;
        float aspect = (float)swapchainExtent.width / (float)swapchainExtent.height;
        float nearP = 0.1f; float farP = 200.0f;
        float f = 1.0f / tan(fov * 0.5f * 3.14159f / 180.0f);
        mat4 p; memset(p.m, 0, sizeof(p.m));
        p.m[0] = f / aspect; p.m[5] = -f; p.m[10] = farP / (nearP - farP); p.m[11] = -1.0f; p.m[14] = (nearP * farP) / (nearP - farP);
        return p;
    }

    void initVulkan() { 
        createInstance(); createSurface(); pickPhysicalDevice(); createLogicalDevice(); createSwapchain(); createImageViews(); 
        createDepthResources();
        createGuiRenderPass(); createGuiFramebuffers();
        createSSBO(); createDescriptors(); createPipeline(); createGraphicsPipeline();
        if(fs::exists(SHADER_SOURCE)) lastShaderWriteTime = fs::last_write_time(SHADER_SOURCE); 
        createCommandPool(); createCommandBuffers(); createSyncObjects(); 
    }

    void createDepthResources() {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        createImage(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
        VkImageCreateInfo imageInfo{}; imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent.width = width; imageInfo.extent.height = height; imageInfo.extent.depth = 1; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.format = format; imageInfo.tiling = tiling; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; imageInfo.usage = usage; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) throw std::runtime_error("failed to create image!");
        VkMemoryRequirements memRequirements; vkGetImageMemoryRequirements(device, image, &memRequirements);
        VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memRequirements.size; allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) throw std::runtime_error("failed to allocate image memory!");
        vkBindImageMemory(device, image, imageMemory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo viewInfo{}; viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = format; viewInfo.subresourceRange.aspectMask = aspectFlags; viewInfo.subresourceRange.baseMipLevel = 0; viewInfo.subresourceRange.levelCount = 1; viewInfo.subresourceRange.baseArrayLayer = 0; viewInfo.subresourceRange.layerCount = 1;
        VkImageView imageView; if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) throw std::runtime_error("failed to create image view!");
        return imageView;
    }
    
    void initImGui() {
        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 } };
        VkDescriptorPoolCreateInfo pool_info = {}; pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool_info.maxSets = 1000; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = pool_sizes;
        vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiDescriptorPool);
        
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); setupImGuiStyle();
        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo init_info = {}; init_info.Instance = instance; init_info.PhysicalDevice = physicalDevice; init_info.Device = device; init_info.QueueFamily = 0; init_info.Queue = presentQueue; init_info.DescriptorPool = imguiDescriptorPool; init_info.MinImageCount = 2; init_info.ImageCount = (uint32_t)swapchainImages.size(); init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&init_info, imguiRenderPass);
        VkCommandBuffer command_buffer = commandBuffers[0]; vkResetCommandBuffer(command_buffer, 0);
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &begin_info); ImGui_ImplVulkan_CreateFontsTexture(command_buffer); vkEndCommandBuffer(command_buffer);
        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit_info.commandBufferCount = 1; submit_info.pCommandBuffers = &command_buffer;
        vkQueueSubmit(computeQueue, 1, &submit_info, VK_NULL_HANDLE); vkDeviceWaitIdle(device); ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    void createGuiRenderPass() {
        VkAttachmentDescription attachment = {}; attachment.format = VK_FORMAT_B8G8R8A8_UNORM; attachment.samples = VK_SAMPLE_COUNT_1_BIT; attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; 
        VkAttachmentReference color_attachment = {}; color_attachment.attachment = 0; color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color_attachment;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency = {}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0; dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dependency.srcAccessMask = 0; dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        std::array<VkAttachmentDescription, 2> attachments = {attachment, depthAttachment};
        VkRenderPassCreateInfo info = {}; info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; info.attachmentCount = (uint32_t)attachments.size(); info.pAttachments = attachments.data(); info.subpassCount = 1; info.pSubpasses = &subpass; info.dependencyCount = 1; info.pDependencies = &dependency;
        vkCreateRenderPass(device, &info, nullptr, &imguiRenderPass);
    }

    void createGuiFramebuffers() {
        imguiFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            std::array<VkImageView, 2> attachments = { swapchainImageViews[i], depthImageView };
            VkFramebufferCreateInfo info = {}; info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; info.renderPass = imguiRenderPass; info.attachmentCount = (uint32_t)attachments.size(); info.pAttachments = attachments.data(); info.width = swapchainExtent.width; info.height = swapchainExtent.height; info.layers = 1;
            vkCreateFramebuffer(device, &info, nullptr, &imguiFramebuffers[i]);
        }
    }

    void createInstance() { VkApplicationInfo a{VK_STRUCTURE_TYPE_APPLICATION_INFO,nullptr,"SDF",1,"",1,VK_API_VERSION_1_3}; uint32_t c; const char** e=glfwGetRequiredInstanceExtensions(&c); VkInstanceCreateInfo i{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,nullptr,0,&a,0,nullptr,c,e}; vkCreateInstance(&i,nullptr,&instance); }
    void createSurface() { glfwCreateWindowSurface(instance,window,nullptr,&surface); }
    void pickPhysicalDevice() { uint32_t c; vkEnumeratePhysicalDevices(instance,&c,nullptr); std::vector<VkPhysicalDevice> d(c); vkEnumeratePhysicalDevices(instance,&c,d.data()); physicalDevice=d[0]; }
    void createLogicalDevice() { 
        float p = 1.0f; VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, 0, 1, &p};
        const char* e[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures deviceFeatures{}; deviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE; 
        VkDeviceCreateInfo i{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &q, 0, nullptr, 1, e, &deviceFeatures};
        vkCreateDevice(physicalDevice, &i, nullptr, &device); vkGetDeviceQueue(device, 0, 0, &computeQueue); vkGetDeviceQueue(device, 0, 0, &presentQueue); 
    }
    void createSwapchain() { VkSurfaceCapabilitiesKHR c; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,surface,&c); if(c.currentExtent.width!=UINT32_MAX) swapchainExtent=c.currentExtent; else { int w,h; glfwGetFramebufferSize(window,&w,&h); swapchainExtent={static_cast<uint32_t>(w),static_cast<uint32_t>(h)}; } VkSwapchainCreateInfoKHR i{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; i.surface=surface; i.minImageCount=c.minImageCount+1; if(c.maxImageCount>0&&i.minImageCount>c.maxImageCount) i.minImageCount=c.maxImageCount; i.imageFormat=VK_FORMAT_B8G8R8A8_UNORM; i.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; i.imageExtent=swapchainExtent; i.imageArrayLayers=1; i.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT; i.preTransform=c.currentTransform; i.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; i.presentMode=VK_PRESENT_MODE_FIFO_KHR; i.clipped=VK_TRUE; vkCreateSwapchainKHR(device,&i,nullptr,&swapchain); uint32_t n; vkGetSwapchainImagesKHR(device,swapchain,&n,nullptr); swapchainImages.resize(n); vkGetSwapchainImagesKHR(device,swapchain,&n,swapchainImages.data()); }
    void createImageViews() { swapchainImageViews.resize(swapchainImages.size()); for(size_t i=0;i<swapchainImages.size();i++) { VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,nullptr,0,swapchainImages[i],VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_B8G8R8A8_UNORM,{},{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; vkCreateImageView(device,&v,nullptr,&swapchainImageViews[i]); } }
    void createDescriptors() { VkDescriptorSetLayoutBinding b[]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT}}; VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,nullptr,0,2,b}; vkCreateDescriptorSetLayout(device,&l,nullptr,&descriptorSetLayout); VkDescriptorPoolSize s[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,(uint32_t)swapchainImages.size()},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,(uint32_t)swapchainImages.size()}}; VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,nullptr,0,(uint32_t)swapchainImages.size(),2,s}; vkCreateDescriptorPool(device,&p,nullptr,&descriptorPool); std::vector<VkDescriptorSetLayout> dl(swapchainImages.size(),descriptorSetLayout); VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,nullptr,descriptorPool,(uint32_t)swapchainImages.size(),dl.data()}; descriptorSets.resize(swapchainImages.size()); vkAllocateDescriptorSets(device,&a,descriptorSets.data()); for(size_t i=0;i<swapchainImages.size();i++) { VkDescriptorImageInfo im{nullptr,swapchainImageViews[i],VK_IMAGE_LAYOUT_GENERAL}; VkDescriptorBufferInfo bu{sceneBuffer,0,sizeof(ObjectData)*MAX_OBJECTS}; VkWriteDescriptorSet w[]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,descriptorSets[i],0,0,1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&im,nullptr,nullptr},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,descriptorSets[i],1,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,nullptr,&bu,nullptr}}; vkUpdateDescriptorSets(device,2,w,0,nullptr); } }
    
    void createGraphicsPipeline() {
        auto virtCode = readFile("mesh_vert.spv");
        auto fragCode = readFile("mesh_frag.spv");
        VkShaderModule vertSM, fragSM;
        VkShaderModuleCreateInfo m1{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,nullptr,0,virtCode.size(),(uint32_t*)virtCode.data()};
        VkShaderModuleCreateInfo m2{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,nullptr,0,fragCode.size(),(uint32_t*)fragCode.data()};
        vkCreateShaderModule(device,&m1,nullptr,&vertSM); vkCreateShaderModule(device,&m2,nullptr,&fragSM);
        
        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vertSM,"main"},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fragSM,"main"}
        };

        VkVertexInputBindingDescription bindingDesc{}; bindingDesc.binding = 0; bindingDesc.stride = sizeof(Vertex); bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrDesc[4];
        attrDesc[0] = {0,0,VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
        attrDesc[1] = {1,0,VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        attrDesc[2] = {2,0,VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        attrDesc[3] = {3,0,VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1; vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = 4; vertexInputInfo.pVertexAttributeDescriptions = attrDesc;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0.0f, 0.0f, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchainExtent};
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL; rasterizer.lineWidth = 1.0f; rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE; depthStencil.depthWriteEnable = VK_TRUE; depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1; colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange p{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(mat4)*2 + sizeof(vec4)*2}; 
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 0; pipelineLayoutInfo.pushConstantRangeCount = 1; pipelineLayoutInfo.pPushConstantRanges = &p;
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout);

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2; pipelineInfo.pStages = stages; pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly; pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer; pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil; pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = graphicsPipelineLayout; pipelineInfo.renderPass = imguiRenderPass; // We use the same render pass as ImGui for simplicity
        pipelineInfo.subpass = 0;
        
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
        vkDestroyShaderModule(device, vertSM, nullptr); vkDestroyShaderModule(device, fragSM, nullptr);
    }

    void createPipeline() { 
        auto c=readFile(SHADER_BINARY); VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,nullptr,0,c.size(),(uint32_t*)c.data()}; 
        VkShaderModule sm; vkCreateShaderModule(device,&m,nullptr,&sm); 
        VkPushConstantRange p{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(PushConstants)}; 
        VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,nullptr,0,1,&descriptorSetLayout,1,&p}; 
        vkCreatePipelineLayout(device,&l,nullptr,&pipelineLayout); 
        VkComputePipelineCreateInfo i{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,nullptr,0,{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,sm,"main"},pipelineLayout}; 
        vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&i,nullptr,&computePipeline); vkDestroyShaderModule(device,sm,nullptr); 
    }

    
    void createCommandPool() { VkCommandPoolCreateInfo i{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,nullptr,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}; vkCreateCommandPool(device,&i,nullptr,&commandPool); }
    void createCommandBuffers() { commandBuffers.resize(swapchainImages.size()); VkCommandBufferAllocateInfo i{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,nullptr,commandPool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,(uint32_t)commandBuffers.size()}; vkAllocateCommandBuffers(device,&i,commandBuffers.data()); }
    void createSyncObjects() { imageAvailable.resize(2); renderFinished.resize(2); inFlightFences.resize(2); VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,nullptr,VK_FENCE_CREATE_SIGNALED_BIT}; for(int i=0;i<2;i++) { vkCreateSemaphore(device,&s,nullptr,&imageAvailable[i]); vkCreateSemaphore(device,&s,nullptr,&renderFinished[i]); vkCreateFence(device,&f,nullptr,&inFlightFences[i]); } }
    
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t i) {
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bi);
        
        VkImageMemoryBarrier b1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b1.srcAccessMask = 0; b1.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT; b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b1.newLayout = VK_IMAGE_LAYOUT_GENERAL; b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b1.image = swapchainImages[i]; b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);
        
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);
        
        PushConstants pc; 
        pc.resolution = { (float)swapchainExtent.width, (float)swapchainExtent.height };
        pc.time=(float)glfwGetTime(); 
        pc.objectCount=(int)sceneGraph.size(); 
        pc.selectedIndex = selectedIndices.empty() ? -1 : selectedIndices.back();
        pc.draggingAxis=(isDraggingGizmo && showGizmos)?currentGizmoAxis:0; 
        pc.mode=currentMode;
        pc.uiMinX = currentUiBounds.x; pc.uiMinY = currentUiBounds.y; pc.uiMaxX = currentUiBounds.z; pc.uiMaxY = currentUiBounds.w;
        pc.shadingMode = currentShadingMode;
        
        // --- GIZMO POSITION FIX ---
        // ИСПОЛЬЗУЕМ МИРОВЫЕ КООРДИНАТЫ (gpuData.position) ВМЕСТО ЛОКАЛЬНЫХ (localPosition)
        vec3 center = {0, 0, 0};
        if (!selectedIndices.empty()) {
            int count = 0;
            for (int idx : selectedIndices) {
                if (idx < sceneGraph.size()) {
                    center.x += sceneGraph[idx].gpuData.position.x; 
                    center.y += sceneGraph[idx].gpuData.position.y;
                    center.z += sceneGraph[idx].gpuData.position.z;
                    count++;
                }
            }
            if (count > 0) {
                center.x /= count; center.y /= count; center.z /= count;
            }
        }
        pc.gizmoPos[0] = center.x; pc.gizmoPos[1] = center.y; pc.gizmoPos[2] = center.z;

        updateCameraMatrix(pc.camToWorld);
        vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
        vkCmdDispatch(cb, (swapchainExtent.width+15)/16, (swapchainExtent.height+15)/16, 1);
        
        VkImageMemoryBarrier b2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; b2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b2.oldLayout = VK_IMAGE_LAYOUT_GENERAL; b2.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b2.image = swapchainImages[i]; b2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);
        
        VkRenderPassBeginInfo info = {}; info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; info.renderPass = imguiRenderPass; info.framebuffer = imguiFramebuffers[i]; info.renderArea.extent = swapchainExtent;
        
        VkClearValue clearValues[2];
        clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        info.clearValueCount = 2;
        info.pClearValues = clearValues;

        vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);
        
        // --- DRAW MESHES ---
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        
        mat4 view = calculateViewMatrix();
        mat4 proj = calculateProjectionMatrix();
        
        for (int i = 0; i < (int)sceneGraph.size(); i++) {
            const auto& node = sceneGraph[i];
            if (node.meshIdx >= 0 && node.meshIdx < meshCache.size()) {
                const auto& mesh = meshCache[node.meshIdx];
                if (!mesh.isLoaded) continue;

                mat4 model = calculateWorldMatrix(i); 
                
                struct MeshPush { mat4 view; mat4 proj; mat4 model; vec4 color; vec4 params; } mp;
                mp.view = view; mp.proj = proj; mp.model = model;
                mp.color = node.gpuData.color;
                mp.params = node.gpuData.params;

                vkCmdPushConstants(cb, graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush), &mp);
                
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cb, 0, 1, &mesh.vertexBuffer.buffer, offsets);
                vkCmdBindIndexBuffer(cb, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, mesh.indexBuffer.count, 1, 0, 0, 0);
            }
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }
    
    void reloadShader() { int result = system(("glslc " + SHADER_SOURCE + " -o " + SHADER_BINARY).c_str()); if (result == 0) { vkDeviceWaitIdle(device); vkDestroyPipeline(device, computePipeline, nullptr); try { createPipeline(); std::cout << "[Shader] Reloaded" << std::endl; } catch(...) {} } }
    void cleanupSwapchain() { 
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);
        for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr); 
        for (auto fb : imguiFramebuffers) vkDestroyFramebuffer(device, fb, nullptr); 
        vkDestroySwapchainKHR(device, swapchain, nullptr); 
        vkDestroyDescriptorPool(device, descriptorPool, nullptr); 
    }
    void recreateSwapchain() { int width=0,height=0; glfwGetFramebufferSize(window,&width,&height); while(width==0||height==0){glfwGetFramebufferSize(window,&width,&height); glfwWaitEvents();} vkDeviceWaitIdle(device); cleanupSwapchain(); createSwapchain(); createImageViews(); createDepthResources(); createGuiFramebuffers(); createDescriptors(); ImGui_ImplVulkan_SetMinImageCount(2); }

    void mainLoop() {
        int currentFrame = 0; double lastTime = glfwGetTime(); int frameCount = 0; double fpsTimer = lastTime;
        while (!glfwWindowShouldClose(window)) {
            double currentTime = glfwGetTime(); float deltaTime = (float)(currentTime - lastTime); lastTime = currentTime;
            frameCount++; if (currentTime - fpsTimer >= 1.0) { std::string title = "Next Level Signed Distance Field Engine | FPS: " + std::to_string(frameCount); glfwSetWindowTitle(window, title.c_str()); frameCount=0; fpsTimer=currentTime; }
            glfwPollEvents(); processInput(window, deltaTime); renderUI(window); 
            if (fs::exists(SHADER_SOURCE)) { try { auto t = fs::last_write_time(SHADER_SOURCE); if (t > lastShaderWriteTime) { vkDeviceWaitIdle(device); reloadShader(); lastShaderWriteTime = t; } } catch(...) {} }
            vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
            updateSSBO();
            vkQueueWaitIdle(computeQueue); 
            uint32_t imageIndex; VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
            if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); continue; }
            vkResetFences(device, 1, &inFlightFences[currentFrame]); 
            recordCommandBuffer(commandBuffers[imageIndex], imageIndex);
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; VkSemaphore ws[]={imageAvailable[currentFrame]}; VkPipelineStageFlags wst[]={VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT}; si.waitSemaphoreCount=1; si.pWaitSemaphores=ws; si.pWaitDstStageMask=wst; si.commandBufferCount=1; si.pCommandBuffers=&commandBuffers[imageIndex]; VkSemaphore ss[]={renderFinished[currentFrame]}; si.signalSemaphoreCount=1; si.pSignalSemaphores=ss;
            vkQueueSubmit(computeQueue, 1, &si, inFlightFences[currentFrame]);
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=ss; pi.swapchainCount=1; pi.pSwapchains=&swapchain; pi.pImageIndices=&imageIndex;
            res = vkQueuePresentKHR(presentQueue, &pi);
            if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) { framebufferResized = false; recreateSwapchain(); }
            currentFrame = (currentFrame + 1) % 2;
        }
        vkDeviceWaitIdle(device);
    }

    void cleanup() {
        vkDeviceWaitIdle(device); 
        for (auto& mesh : meshCache) {
            if (mesh.isLoaded) {
                vkDestroyBuffer(device, mesh.vertexBuffer.buffer, nullptr); vkFreeMemory(device, mesh.vertexBuffer.memory, nullptr);
                vkDestroyBuffer(device, mesh.indexBuffer.buffer, nullptr); vkFreeMemory(device, mesh.indexBuffer.memory, nullptr);
            }
        }
        vkDestroyPipeline(device, graphicsPipeline, nullptr); vkDestroyPipelineLayout(device, graphicsPipelineLayout, nullptr);
        if(logoSampler) vkDestroySampler(device, logoSampler, nullptr); if(logoImageView) vkDestroyImageView(device, logoImageView, nullptr); if(logoImage) vkDestroyImage(device, logoImage, nullptr); if(logoImageMemory) vkFreeMemory(device, logoImageMemory, nullptr);
        ImGui_ImplVulkan_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
        vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr); vkDestroyRenderPass(device, imguiRenderPass, nullptr);
        cleanupSwapchain(); 
        vkDestroyPipeline(device, computePipeline, nullptr); vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkUnmapMemory(device, sceneBufferMemory); vkDestroyBuffer(device, sceneBuffer, nullptr); vkFreeMemory(device, sceneBufferMemory, nullptr);
        for(int i=0; i<2; i++) { vkDestroySemaphore(device, imageAvailable[i], nullptr); vkDestroySemaphore(device, renderFinished[i], nullptr); vkDestroyFence(device, inFlightFences[i], nullptr); }
        vkDestroyCommandPool(device, commandPool, nullptr); vkDestroyDevice(device, nullptr); vkDestroySurfaceKHR(instance, surface, nullptr); vkDestroyInstance(instance, nullptr); glfwDestroyWindow(window); glfwTerminate();
    }
};

void* g_engine = nullptr;
void Engine_PublicSaveNos(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->PublicSaveNos(path); }
void Engine_PublicLoadNos(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->PublicLoadNos(path); }
void Engine_LoadMesh(const std::string& path) { if (g_engine) reinterpret_cast<Engine*>(g_engine)->LoadMeshInternal(path); }

int main() { 
    try { Engine e; e.run(); } 
    catch (const std::exception& x) { std::cerr << "\n[CRITICAL ERROR]: " << x.what() << std::endl; std::cin.get(); return EXIT_FAILURE; } 
    return EXIT_SUCCESS; 
}