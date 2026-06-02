#pragma once

#include <vector>
#include <string>
#include <filesystem>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "vk_mem_alloc.h"
#include "Common.h"

namespace fs = std::filesystem;

class Renderer {
public:
    void init(GLFWwindow* window);
    void cleanup();
    void drawFrame(GLFWwindow* window, bool& framebufferResized);
    void recreateSwapchain(GLFWwindow* window);
    void updateSSBO();
    void reloadShader();
    
    // Helpers for Engine
    void createBuffer(VkDeviceSize s, VkBufferUsageFlags u, VmaMemoryUsage vmaUsage, VkBuffer& b, VmaAllocation& a);
    std::vector<uint8_t> captureScreenshot(VkExtent2D swapchainExtent, const std::vector<VkImage>& swapchainImages);
    void setupInternalLogo(GLFWwindow* window);

    // Getters for needed properties
    VkDevice getDevice() { return device; }
    VmaAllocator getAllocator() { return allocator; }
    VkExtent2D getSwapchainExtent() { return swapchainExtent; }
    const std::vector<VkImage>& getSwapchainImages() { return swapchainImages; }

private:
    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain(GLFWwindow* window);
    void createImageViews();
    void createDepthResources();
    void createSdfDepthResources();
    void createGuiRenderPass();
    void createGuiFramebuffers();
    void createSSBO();
    void createDescriptors();
    void createPipeline();
    void createGraphicsPipeline();
    void createDepthPrimePipeline();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void initImGui(GLFWwindow* window);
    
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t i);
    void cleanupSwapchain();
    
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage vmaUsage, VkImage& image, VmaAllocation& allocation);

    // Vulkan objects
    VmaAllocator allocator;
    VkInstance instance;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue computeQueue, presentQueue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkExtent2D swapchainExtent;
    
    VkPipelineLayout pipelineLayout;
    VkPipeline computePipeline;
    VkPipelineLayout graphicsPipelineLayout;
    VkPipeline graphicsPipeline;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;
    
    VkImage depthImage;
    VmaAllocation depthImageAllocation;
    VkImageView depthImageView;
    
    std::vector<VkImage> sdfDepthImages;
    std::vector<VmaAllocation> sdfDepthImageAllocations;
    std::vector<VkImageView> sdfDepthImageViews;
    
    VkPipeline depthPrimePipeline;
    VkPipelineLayout depthPrimePipelineLayout;
    VkDescriptorSetLayout depthPrimeSetLayout;
    VkSampler depthPrimeSampler;
    std::vector<VkDescriptorSet> depthPrimeSets;
    
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable, renderFinished;
    std::vector<VkFence> inFlightFences;
    
    VkBuffer sceneBuffer;
    VmaAllocation sceneBufferAllocation;
    void* sceneBufferMapped = nullptr;
    
    VkDescriptorPool imguiDescriptorPool;
    VkRenderPass imguiRenderPass;
    std::vector<VkFramebuffer> imguiFramebuffers;
    
    int currentFrame = 0;
    fs::file_time_type lastShaderWriteTime;

    // Logo texture objects
    VkImage logoImage = VK_NULL_HANDLE;
    VmaAllocation logoImageAllocation = VK_NULL_HANDLE;
    VkImageView logoImageView = VK_NULL_HANDLE;
    VkSampler logoSampler = VK_NULL_HANDLE;
    
    void createLogoTexture(const std::vector<unsigned char>& pixels, int w, int h);
};
