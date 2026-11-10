#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include "Common.h"

namespace fs = std::filesystem;

struct OBJModel;

void uiNewFrame();   // Начало кадра ImGui (бэкенды Vulkan + GLFW)
std::string getExecutablePath();
std::string getExecutableDir();

// Раскладка должна совпадать с блоком Frame в shaders/shader.comp (std140)
struct FrameUBO {
    mat4 camToWorld;
    vec4 viewParams;      // xy = разрешение рендера, z = focal length
    glm::ivec4 counts;    // x = объекты, y = источники света, z = shading mode, w = флаги
    glm::ivec4 quality;   // x = шаги марша, y = режим теней, z = шаги теней, w = отскоки
    vec4 sunDir;
    vec4 sunColor;
    vec4 skyTop;
    vec4 skyHorizon;
    vec4 lightPos[MAX_LIGHTS];
    vec4 lightCol[MAX_LIGHTS];
};

struct RenderStats {
    std::string gpuName;
    int deviceType = 0;            // VkPhysicalDeviceType
    bool hasTimestamps = false;
    float gpuMs = 0.0f;            // Время compute-прохода последнего отрендеренного кадра
    double gpuMsSum = 0.0;
    uint64_t gpuMsCount = 0;
    int renderW = 0, renderH = 0;  // Разрешение SDF-кадра
    int viewW = 0, viewH = 0;      // Размер вьюпорта
    bool renderedThisFrame = false;
    uint64_t framesRendered = 0;
    uint64_t framesReused = 0;
    uint32_t validationErrors = 0;
};
extern RenderStats renderStats;

class Renderer {
public:
    void init(GLFWwindow* window);
    void cleanup();
    void drawFrame(GLFWwindow* window, bool& framebufferResized);
    void recreateSwapchain(GLFWwindow* window);
    void reloadShader();
    void requestRedraw() { forceRender = true; }

    // Рисовать SDF каждый кадр в полном качестве (бенчмарк)
    bool continuous = false;
    // Сетка пола — оверлей редактора; в игре приложение её выключает
    bool drawGrid = true;

    // Helpers for Engine
    void createBuffer(VkDeviceSize s, VkBufferUsageFlags u, VmaMemoryUsage vmaUsage, VkBuffer& b, VmaAllocation& a);
    bool uploadMesh(const OBJModel& model, MeshData& out);
    std::vector<uint8_t> captureScreenshot(int& outW, int& outH);   // RGBA последнего SDF-кадра (без UI)
    void setupInternalLogo(GLFWwindow* window);

    // Текстуры для ImGui (миниатюры ассетов). Возвращает ImTextureID.
    void* createTexture(const uint8_t* rgba, int w, int h);
    void destroyTexture(void* textureId);

    VkDevice getDevice() { return device; }
    VmaAllocator getAllocator() { return allocator; }
    VkExtent2D getSwapchainExtent() { return swapchainExtent; }

private:
    static constexpr int FRAMES_IN_FLIGHT = 2;

    struct FramePlan {
        bool hasViewport = false;
        bool render = false;
        bool interactive = false;
        VkRect2D viewport{};     // Вьюпорт в пикселях окна
        uint32_t renderW = 0, renderH = 0;
        float scale = 1.0f;
    };

    struct UiTexture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        void* id = nullptr;
    };

    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain(GLFWwindow* window);
    void createImageViews();
    void createDepthResources();
    void createOffscreenResources();
    void createSamplers();
    void createRenderPass();
    void createFramebuffers();
    void createSceneBuffers();
    void createDescriptors();
    void updateImageDescriptors();
    void createComputePipeline();
    void createGraphicsPipeline();
    void createCompositePipeline();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createQueryPool();
    void initImGui(GLFWwindow* window);

    FramePlan planFrame(FrameUBO& ubo);
    void readTimestamps(int slot);
    void checkShaderHotReload();
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, const FramePlan& plan);
    void cleanupSwapchain();

    VkShaderModule loadShaderModule(const std::string& spvName);
    VkCommandBuffer beginOneShot();
    void endOneShot(VkCommandBuffer cb);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation);
    UiTexture createTextureInternal(const uint8_t* rgba, int w, int h);

    // Vulkan objects
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkExtent2D swapchainExtent{};

    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // SDF рисуется сюда compute-шейдером, затем растягивается на вьюпорт
    VkImage sdfColorImage = VK_NULL_HANDLE, sdfDepthImage = VK_NULL_HANDLE;
    VmaAllocation sdfColorAllocation = VK_NULL_HANDLE, sdfDepthAllocation = VK_NULL_HANDLE;
    VkImageView sdfColorView = VK_NULL_HANDLE, sdfDepthView = VK_NULL_HANDLE;
    bool offscreenReady = false;
    VkSampler linearSampler = VK_NULL_HANDLE, nearestSampler = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkPipelineLayout computeLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout meshLayout = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout = VK_NULL_HANDLE;
    VkPipeline compositePipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet computeSets[FRAMES_IN_FLIGHT]{};
    VkDescriptorSet compositeSet = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT]{};
    VkSemaphore imageAvailable[FRAMES_IN_FLIGHT]{};
    std::vector<VkSemaphore> renderFinished;   // По одному на картинку swapchain: present держит семафор до её повторного захвата
    VkFence inFlightFences[FRAMES_IN_FLIGHT]{};

    // Данные сцены и кадра — свои на каждый кадр в полёте, чтобы не ждать GPU
    VkBuffer sceneBuffers[FRAMES_IN_FLIGHT]{}, uboBuffers[FRAMES_IN_FLIGHT]{};
    VmaAllocation sceneAllocations[FRAMES_IN_FLIGHT]{}, uboAllocations[FRAMES_IN_FLIGHT]{};
    void* sceneMapped[FRAMES_IN_FLIGHT]{};
    void* uboMapped[FRAMES_IN_FLIGHT]{};

    // Замер времени compute-прохода
    VkQueryPool queryPool = VK_NULL_HANDLE;
    float timestampPeriod = 1.0f;
    bool slotQueryValid[FRAMES_IN_FLIGHT]{};
    bool slotInteractive[FRAMES_IN_FLIGHT]{};
    float slotScale[FRAMES_IN_FLIGHT]{};

    // Рендер по требованию
    uint64_t lastSceneHash = 0;
    bool forceRender = true;
    bool lastRenderInteractive = false;
    int stillFrames = 1000;
    uint32_t lastRenderW = 0, lastRenderH = 0;
    float dynamicScale = 0.5f;
    double lastFrameTime = 0.0;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    int currentFrame = 0;
    fs::file_time_type lastShaderWriteTime;
    double lastShaderCheck = 0.0;

    std::vector<UiTexture> uiTextures;
    VkSampler uiSampler = VK_NULL_HANDLE;
};
