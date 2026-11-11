#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "Scene.h"
#include "Settings.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <array>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

RenderStats renderStats;

namespace {
    const int FLAG_AO = 1;
    const int FLAG_GRID = 2;
    const int FLAG_LIGHT_SHADOWS = 4;
    const float NEAR_PLANE = 0.1f;
    const float FAR_PLANE = 200.0f;

    void vkCheck(VkResult r, const char* what) {
        if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string((int)r) + ")");
    }

    uint64_t fnv1a(const void* data, size_t size, uint64_t h = 1469598103934665603ull) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { renderStats.validationErrors++; spdlog::error("[Vulkan] {}", data->pMessage); }
        else spdlog::warn("[Vulkan] {}", data->pMessage);
        return VK_FALSE;
    }

    const char* deviceTypeName(VkPhysicalDeviceType t) {
        switch (t) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
            case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
            default: return "other";
        }
    }
}

std::string getExecutablePath() {
#ifdef _WIN32
    char result[MAX_PATH];
    DWORD count = GetModuleFileNameA(NULL, result, MAX_PATH);
    return std::string(result, count);
#else
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    return std::string(result, (count > 0) ? count : 0);
#endif
}

std::string getExecutableDir() {
    return fs::path(getExecutablePath()).parent_path().string();
}

std::string findShaderSource() {
    std::string path1 = getExecutableDir() + "/" + SHADER_SOURCE;
    if (fs::exists(path1)) return path1;
    std::string path2 = getExecutableDir() + "/../" + SHADER_SOURCE;
    if (fs::exists(path2)) return path2;
    return SHADER_SOURCE;
}

// =================================================================
// --- Init / Cleanup ---
// =================================================================
void Renderer::init(GLFWwindow* window) {
    createInstance();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator), "vmaCreateAllocator");

    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createQueryPool();
    createSamplers();

    createSwapchain(window);
    createImageViews();
    createDepthResources();
    createOffscreenResources();
    createRenderPass();
    createFramebuffers();
    createSceneBuffers();
    createDescriptors();
    createComputePipeline();
    createGraphicsPipeline();
    createCompositePipeline();

    std::error_code ec;
    std::string shaderPath = findShaderSource();
    if (fs::exists(shaderPath, ec)) lastShaderWriteTime = fs::last_write_time(shaderPath, ec);

    dynamicScale = gfx.interactiveScale;
    initImGui(window);
}

void Renderer::cleanup() {
    vkDeviceWaitIdle(device);
    for (auto& mesh : meshCache) {
        if (mesh.isLoaded) {
            vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
            vmaDestroyBuffer(allocator, mesh.indexBuffer.buffer, mesh.indexBuffer.allocation);
        }
    }
    meshCache.clear();

    for (auto& tex : uiTextures) {
        vkDestroyImageView(device, tex.view, nullptr);
        vmaDestroyImage(allocator, tex.image, tex.allocation);
    }
    uiTextures.clear();
    if (uiSampler) vkDestroySampler(device, uiSampler, nullptr);

    ImGui_ImplVulkan_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);

    cleanupSwapchain();
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyPipeline(device, computePipeline, nullptr); vkDestroyPipelineLayout(device, computeLayout, nullptr);
    vkDestroyPipeline(device, meshPipeline, nullptr); vkDestroyPipelineLayout(device, meshLayout, nullptr);
    vkDestroyPipeline(device, compositePipeline, nullptr); vkDestroyPipelineLayout(device, compositeLayout, nullptr);
    if (volumeBuffer) vmaDestroyBuffer(allocator, volumeBuffer, volumeAllocation);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, computeSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, compositeSetLayout, nullptr);
    vkDestroySampler(device, linearSampler, nullptr);
    vkDestroySampler(device, nearestSampler, nullptr);
    if (queryPool) vkDestroyQueryPool(device, queryPool, nullptr);

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vmaUnmapMemory(allocator, sceneAllocations[i]); vmaDestroyBuffer(allocator, sceneBuffers[i], sceneAllocations[i]);
        vmaUnmapMemory(allocator, uboAllocations[i]); vmaDestroyBuffer(allocator, uboBuffers[i], uboAllocations[i]);
        vkDestroySemaphore(device, imageAvailable[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);

    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    if (debugMessenger) {
        auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFn) destroyFn(instance, debugMessenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
}

void Renderer::createInstance() {
    // Vulkan 1.0 достаточно: движок не использует ничего новее, а старые драйверы 1.3 не умеют
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NLSDFE"; app.applicationVersion = 1;
    app.pEngineName = "NLSDFE"; app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
    if (!glfwExt) throw std::runtime_error("Vulkan is not available on this system (no driver or loader found)");
    std::vector<const char*> extensions(glfwExt, glfwExt + glfwCount);
    std::vector<const char*> layers;

    // NLSDFE_VALIDATION=1 включает слой валидации, если он установлен
    const char* wantValidation = std::getenv("NLSDFE_VALIDATION");
    bool validation = false;
    if (wantValidation && wantValidation[0] == '1') {
        uint32_t layerCount = 0; vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount); vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        for (const auto& l : available) if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) validation = true;
        if (validation) { layers.push_back("VK_LAYER_KHRONOS_validation"); extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }
        else spdlog::warn("Validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = (uint32_t)extensions.size(); info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = (uint32_t)layers.size(); info.ppEnabledLayerNames = layers.data();
    vkCheck(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");

    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg.pfnUserCallback = debugCallback;
        auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (createFn) createFn(instance, &dbg, nullptr, &debugMessenger);
        spdlog::info("Vulkan validation enabled");
    }
}

void Renderer::createSurface(GLFWwindow* window) {
    vkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
}

void Renderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    // Нужна одна очередь, умеющая graphics + compute + present, и swapchain
    auto findQueue = [&](VkPhysicalDevice d, uint32_t& outFamily) {
        uint32_t extCount = 0; vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount); vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for (const auto& e : exts) if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) hasSwapchain = true;
        if (!hasSwapchain) return false;

        uint32_t qCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(qCount); vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, families.data());
        for (uint32_t i = 0; i < qCount; i++) {
            const VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            VkBool32 present = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
            if ((families[i].queueFlags & need) == need && present) { outFamily = i; return true; }
        }
        return false;
    };

    int best = -1, bestScore = -1;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(devices[i], &props);
        uint32_t family = 0;
        bool ok = findQueue(devices[i], family);
        spdlog::info("GPU {}: {} ({}){}", i, props.deviceName, deviceTypeName(props.deviceType), ok ? "" : " - unsupported");
        if (!ok) continue;
        int score = 1;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 4;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 3;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 2;
        if ((int)i == gfx.gpuIndex) score = 100;   // Явный выбор пользователя
        if (score > bestScore) { bestScore = score; best = (int)i; }
    }
    if (best < 0) throw std::runtime_error("No suitable GPU: need a queue with graphics + compute + present and swapchain support");

    physicalDevice = devices[best];
    findQueue(physicalDevice, queueFamily);

    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physicalDevice, &props);
    uint32_t qCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount); vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, families.data());

    timestampPeriod = props.limits.timestampPeriod;
    renderStats.gpuName = props.deviceName;
    renderStats.deviceType = (int)props.deviceType;
    renderStats.hasTimestamps = families[queueFamily].timestampValidBits > 0 && timestampPeriod > 0.0f;
    spdlog::info("Using GPU {}: {} (Vulkan {}.{})", best, props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
}

void Renderer::createLogicalDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queueFamily; q.queueCount = 1; q.pQueuePriorities = &priority;
    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features{};   // Никаких необязательных фич не требуем
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = extensions;
    info.pEnabledFeatures = &features;
    vkCheck(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
}

// =================================================================
// --- Swapchain & Render Targets ---
// =================================================================
void Renderer::createSwapchain(GLFWwindow* window) {
    VkSurfaceCapabilitiesKHR caps;
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (caps.currentExtent.width != UINT32_MAX) swapchainExtent = caps.currentExtent;
    else {
        int w, h; glfwGetFramebufferSize(window, &w, &h);
        swapchainExtent.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
        swapchainExtent.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t formatCount = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount); vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    if (formats.empty()) throw std::runtime_error("Surface reports no formats");
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) if (f.format == VK_FORMAT_R8G8B8A8_UNORM) chosen = f;
    for (const auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = f;
    swapchainFormat = chosen.format;

    uint32_t modeCount = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount); vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!gfx.vsync) {
        for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = m;
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
    }

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
              : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
    }

    VkSwapchainKHR old = swapchain;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && info.minImageCount > caps.maxImageCount) info.minImageCount = caps.maxImageCount;
    info.imageFormat = chosen.format; info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = swapchainExtent; info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = presentMode; info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    vkCheck(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
    if (old) vkDestroySwapchainKHR(device, old, nullptr);

    uint32_t n = 0; vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swapchainImages.resize(n); vkGetSwapchainImagesKHR(device, swapchain, &n, swapchainImages.data());
}

void Renderer::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());
    renderFinished.resize(swapchainImages.size());
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        swapchainImageViews[i] = createImageView(swapchainImages[i], swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        vkCheck(vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished[i]), "vkCreateSemaphore");
    }
}

void Renderer::createBuffer(VkDeviceSize s, VkBufferUsageFlags u, VmaMemoryUsage vmaUsage, VkBuffer& b, VmaAllocation& a) {
    VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = s;
    bufferInfo.usage = u;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = vmaUsage;

    vkCheck(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &b, &a, nullptr), "vmaCreateBuffer");
}

void Renderer::createDepthResources() {
    // D32 не гарантирован стандартом — берём первый поддерживаемый формат
    const VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM };
    for (VkFormat f : candidates) {
        VkFormatProperties props; vkGetPhysicalDeviceFormatProperties(physicalDevice, f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat = f; break; }
    }
    createImage(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthImageAllocation);
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::createOffscreenResources() {
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    createImage(swapchainExtent.width, swapchainExtent.height, VK_FORMAT_R8G8B8A8_UNORM, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, sdfColorImage, sdfColorAllocation);
    createImage(swapchainExtent.width, swapchainExtent.height, VK_FORMAT_R32_SFLOAT, usage, sdfDepthImage, sdfDepthAllocation);
    sdfColorView = createImageView(sdfColorImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    sdfDepthView = createImageView(sdfDepthImage, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    offscreenReady = false;
    forceRender = true;
}

void Renderer::createSamplers() {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1.0f;
    info.magFilter = info.minFilter = VK_FILTER_NEAREST;
    vkCheck(vkCreateSampler(device, &info, nullptr, &nearestSampler), "vkCreateSampler");
    info.magFilter = info.minFilter = VK_FILTER_LINEAR;
    vkCheck(vkCreateSampler(device, &info, nullptr, &linearSampler), "vkCreateSampler");
    vkCheck(vkCreateSampler(device, &info, nullptr, &uiSampler), "vkCreateSampler");
}

void Renderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image, VmaAllocation& allocation) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vkCheck(vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr), "vmaCreateImage");
}

VkImageView Renderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = format;
    viewInfo.subresourceRange = { aspectFlags, 0, 1, 0, 1 };
    VkImageView imageView;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &imageView), "vkCreateImageView");
    return imageView;
}

void Renderer::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentDescription depth{};
    depth.format = depthFormat; depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { color, depth };
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = (uint32_t)attachments.size(); info.pAttachments = attachments.data();
    info.subpassCount = 1; info.pSubpasses = &subpass;
    info.dependencyCount = 1; info.pDependencies = &dependency;
    vkCheck(vkCreateRenderPass(device, &info, nullptr, &renderPass), "vkCreateRenderPass");
}

void Renderer::createFramebuffers() {
    framebuffers.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { swapchainImageViews[i], depthImageView };
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = renderPass;
        info.attachmentCount = (uint32_t)attachments.size(); info.pAttachments = attachments.data();
        info.width = swapchainExtent.width; info.height = swapchainExtent.height; info.layers = 1;
        vkCheck(vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
    }
}

void Renderer::createSceneBuffers() {
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        createBuffer(sizeof(GpuObject) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sceneBuffers[i], sceneAllocations[i]);
        vkCheck(vmaMapMemory(allocator, sceneAllocations[i], &sceneMapped[i]), "vmaMapMemory");
        memset(sceneMapped[i], 0, sizeof(GpuObject) * MAX_OBJECTS);
        createBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, uboBuffers[i], uboAllocations[i]);
        vkCheck(vmaMapMemory(allocator, uboAllocations[i], &uboMapped[i]), "vmaMapMemory");
        memset(uboMapped[i], 0, sizeof(FrameUBO));
    }
}

void Renderer::createDescriptors() {
    VkDescriptorSetLayoutBinding computeBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo computeInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    computeInfo.bindingCount = 5; computeInfo.pBindings = computeBindings;
    vkCheck(vkCreateDescriptorSetLayout(device, &computeInfo, nullptr, &computeSetLayout), "vkCreateDescriptorSetLayout");

    VkDescriptorSetLayoutBinding compositeBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo compositeInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    compositeInfo.bindingCount = 2; compositeInfo.pBindings = compositeBindings;
    vkCheck(vkCreateDescriptorSetLayout(device, &compositeInfo, nullptr, &compositeSetLayout), "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = FRAMES_IN_FLIGHT + 1; poolInfo.poolSizeCount = 4; poolInfo.pPoolSizes = sizes;
    vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    VkDescriptorSetLayout layouts[FRAMES_IN_FLIGHT + 1];
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) layouts[i] = computeSetLayout;
    layouts[FRAMES_IN_FLIGHT] = compositeSetLayout;
    VkDescriptorSet sets[FRAMES_IN_FLIGHT + 1];
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool; alloc.descriptorSetCount = FRAMES_IN_FLIGHT + 1; alloc.pSetLayouts = layouts;
    vkCheck(vkAllocateDescriptorSets(device, &alloc, sets), "vkAllocateDescriptorSets");
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) computeSets[i] = sets[i];
    compositeSet = sets[FRAMES_IN_FLIGHT];

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo sceneInfo{sceneBuffers[i], 0, sizeof(GpuObject) * MAX_OBJECTS};
        VkDescriptorBufferInfo frameInfo{uboBuffers[i], 0, sizeof(FrameUBO)};
        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = computeSets[i]; writes[0].dstBinding = 1;
        writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &sceneInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = computeSets[i]; writes[1].dstBinding = 3;
        writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[1].pBufferInfo = &frameInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
    updateImageDescriptors();
    uploadVolumes();
}

// Воксельные сетки: один общий буфер, пересоздаётся, когда набор сеток в сцене поменялся
void Renderer::uploadVolumes() {
    uploadedVolumeVersion = volumeDataVersion;
    VkDeviceSize bytes = std::max<VkDeviceSize>(sizeof(float) * volumeData.size(), 16);
    if (volumeBuffer) {
        vkDeviceWaitIdle(device);   // Старый буфер может читать кадр, который ещё в работе
        vmaDestroyBuffer(allocator, volumeBuffer, volumeAllocation);
        volumeBuffer = VK_NULL_HANDLE;
    }
    createBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, volumeBuffer, volumeAllocation);
    if (!volumeData.empty()) {
        void* data;
        vkCheck(vmaMapMemory(allocator, volumeAllocation, &data), "vmaMapMemory");
        memcpy(data, volumeData.data(), volumeData.size() * sizeof(float));
        vmaFlushAllocation(allocator, volumeAllocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(allocator, volumeAllocation);
    }
    VkDescriptorBufferInfo info{volumeBuffer, 0, VK_WHOLE_SIZE};
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = computeSets[i]; write.dstBinding = 4;
        write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    forceRender = true;
}

// Вызывается после каждого пересоздания offscreen-картинок
void Renderer::updateImageDescriptors() {
    VkDescriptorImageInfo colorStore{VK_NULL_HANDLE, sdfColorView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo depthStore{VK_NULL_HANDLE, sdfDepthView, VK_IMAGE_LAYOUT_GENERAL};
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = computeSets[i]; writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[0].pImageInfo = &colorStore;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = computeSets[i]; writes[1].dstBinding = 2;
        writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[1].pImageInfo = &depthStore;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
    VkDescriptorImageInfo colorSample{linearSampler, sdfColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depthSample{nearestSampler, sdfDepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = compositeSet; writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo = &colorSample;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = compositeSet; writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].pImageInfo = &depthSample;
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

// =================================================================
// --- Pipelines ---
// =================================================================
VkShaderModule Renderer::loadShaderModule(const std::string& spvName) {
    auto code = readFile(getExecutableDir() + "/" + spvName);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size(); info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    vkCheck(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

void Renderer::createComputePipeline() {
    if (!computeLayout) {
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &computeSetLayout;
        vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &computeLayout), "vkCreatePipelineLayout");
    }
    VkShaderModule module = loadShaderModule(SHADER_BINARY);
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    info.layout = computeLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    vkCheck(r, "vkCreateComputePipelines");
    if (computePipeline) vkDestroyPipeline(device, computePipeline, nullptr);
    computePipeline = pipeline;
}

namespace {
    // Общие для mesh- и composite-пайплайнов настройки
    struct RasterState {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState blendAttachment{};
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

        RasterState() {
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            viewport.viewportCount = 1; viewport.scissorCount = 1;
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0f; raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            depth.depthTestEnable = VK_TRUE; depth.depthWriteEnable = VK_TRUE; depth.depthCompareOp = VK_COMPARE_OP_LESS;
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend.attachmentCount = 1; blend.pAttachments = &blendAttachment;
            dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
        }
    };

    struct MeshPush { mat4 mvp; vec4 normalRow0, normalRow1, normalRow2; vec4 color; };
    struct CompositePush { vec4 rect; vec4 uvScale; };
    static_assert(sizeof(MeshPush) == 128, "MeshPush must fit the guaranteed 128 bytes of push constants");
}

void Renderer::createGraphicsPipeline() {
    VkShaderModule vert = loadShaderModule("mesh_vert.spv");
    VkShaderModule frag = loadShaderModule("mesh_frag.spv");
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr}
    };

    VkVertexInputBindingDescription bindingDesc{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrDesc[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1; vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 4; vertexInput.pVertexAttributeDescriptions = attrDesc;

    RasterState state;
    state.raster.cullMode = VK_CULL_MODE_BACK_BIT;

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &meshLayout), "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &state.inputAssembly; info.pViewportState = &state.viewport;
    info.pRasterizationState = &state.raster; info.pMultisampleState = &state.multisample;
    info.pDepthStencilState = &state.depth; info.pColorBlendState = &state.blend;
    info.pDynamicState = &state.dynamic;
    info.layout = meshLayout; info.renderPass = renderPass; info.subpass = 0;
    VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &meshPipeline);
    vkDestroyShaderModule(device, vert, nullptr); vkDestroyShaderModule(device, frag, nullptr);
    vkCheck(r, "vkCreateGraphicsPipelines(mesh)");
}

void Renderer::createCompositePipeline() {
    VkShaderModule vert = loadShaderModule("composite_vert.spv");
    VkShaderModule frag = loadShaderModule("composite_frag.spv");
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr}
    };

    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &compositeSetLayout;
    layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &push;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &compositeLayout), "vkCreatePipelineLayout");

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    RasterState state;
    state.depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;   // Просто переносим глубину SDF в буфер

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &state.inputAssembly; info.pViewportState = &state.viewport;
    info.pRasterizationState = &state.raster; info.pMultisampleState = &state.multisample;
    info.pDepthStencilState = &state.depth; info.pColorBlendState = &state.blend;
    info.pDynamicState = &state.dynamic;
    info.layout = compositeLayout; info.renderPass = renderPass; info.subpass = 0;
    VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &compositePipeline);
    vkDestroyShaderModule(device, vert, nullptr); vkDestroyShaderModule(device, frag, nullptr);
    vkCheck(r, "vkCreateGraphicsPipelines(composite)");
}

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; info.queueFamilyIndex = queueFamily;
    vkCheck(vkCreateCommandPool(device, &info, nullptr, &commandPool), "vkCreateCommandPool");
}

void Renderer::createCommandBuffers() {
    VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = commandPool; info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; info.commandBufferCount = FRAMES_IN_FLIGHT;
    vkCheck(vkAllocateCommandBuffers(device, &info, commandBuffers), "vkAllocateCommandBuffers");
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vkCheck(vkCreateSemaphore(device, &s, nullptr, &imageAvailable[i]), "vkCreateSemaphore");
        vkCheck(vkCreateFence(device, &f, nullptr, &inFlightFences[i]), "vkCreateFence");
    }
}

void Renderer::createQueryPool() {
    if (!renderStats.hasTimestamps) return;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = FRAMES_IN_FLIGHT * 2;
    if (vkCreateQueryPool(device, &info, nullptr, &queryPool) != VK_SUCCESS) { queryPool = VK_NULL_HANDLE; renderStats.hasTimestamps = false; }
}

VkCommandBuffer Renderer::beginOneShot() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandPool = commandPool; alloc.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkCheck(vkAllocateCommandBuffers(device, &alloc, &cb), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);
    return cb;
}

void Renderer::endOneShot(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cb;
    vkCheck(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &cb);
}

void uiNewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

// =================================================================
// --- Frame ---
// =================================================================
void Renderer::checkShaderHotReload() {
    // Раньше файл проверялся каждый кадр — хватит и пары раз в секунду
    double now = glfwGetTime();
    if (now - lastShaderCheck < 0.5) return;
    lastShaderCheck = now;
    std::error_code ec;
    std::string shaderPath = findShaderSource();
    if (!fs::exists(shaderPath, ec)) return;
    auto t = fs::last_write_time(shaderPath, ec);
    if (!ec && t > lastShaderWriteTime) {
        lastShaderWriteTime = t;
        reloadShader();
    }
}

void Renderer::reloadShader() {
    std::string srcPath = findShaderSource();
    std::string binPath = getExecutableDir() + "/" + SHADER_BINARY;
    std::string tmpPath = binPath + ".tmp";
    std::string cmd = "glslc \"" + srcPath + "\" -o \"" + tmpPath + "\"";
    if (system(cmd.c_str()) != 0) { spdlog::error("Shader compilation failed: {}", cmd); showToast("Shader compile error"); return; }

    std::error_code ec;
    fs::rename(tmpPath, binPath, ec);
    if (ec) { spdlog::error("Cannot replace {}", binPath); return; }
    vkDeviceWaitIdle(device);
    try { createComputePipeline(); forceRender = true; spdlog::info("Shader reloaded"); showToast("Shader reloaded"); }
    catch (const std::exception& e) { spdlog::error("Failed to create pipeline after shader reload: {}", e.what()); }
}

void Renderer::readTimestamps(int slot) {
    if (!queryPool || !slotQueryValid[slot]) return;
    slotQueryValid[slot] = false;
    uint64_t times[2] = {};
    VkResult r = vkGetQueryPoolResults(device, queryPool, slot * 2, 2, sizeof(times), times, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS || times[1] < times[0]) return;
    float ms = (float)((double)(times[1] - times[0]) * timestampPeriod * 1e-6);
    renderStats.gpuMs = ms;
    renderStats.gpuMsSum += ms;
    renderStats.gpuMsCount++;

    // Динамическое разрешение: подгоняем масштаб движения под бюджет кадра
    if (slotInteractive[slot] && gfx.autoScale && ms > 0.01f) {
        float budget = 1000.0f / (float)gfx.targetFps * 0.8f;
        float ideal = slotScale[slot] * std::sqrt(budget / ms);
        dynamicScale = std::clamp(dynamicScale + (ideal - dynamicScale) * 0.25f, 0.2f, 1.0f);
    }
}

Renderer::FramePlan Renderer::planFrame(FrameUBO& ubo) {
    FramePlan plan;

    // Вьюпорт из координат окна в пиксели framebuffer'а
    ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
    int W = (int)swapchainExtent.width, H = (int)swapchainExtent.height;
    int vx = std::clamp((int)std::lround(viewportRect.x * fbScale.x), 0, W);
    int vy = std::clamp((int)std::lround(viewportRect.y * fbScale.y), 0, H);
    int vw = std::clamp((int)std::lround(viewportRect.z * fbScale.x), 0, W - vx);
    int vh = std::clamp((int)std::lround(viewportRect.w * fbScale.y), 0, H - vy);
    renderStats.viewW = vw; renderStats.viewH = vh;
    if (vw < 8 || vh < 8) return plan;
    plan.hasViewport = true;
    plan.viewport = { {vx, vy}, {(uint32_t)vw, (uint32_t)vh} };

    CameraBasis b = getCameraBasis();
    mat4 view = glm::lookAt(b.pos, b.pos + b.fwd, b.up);
    ubo = FrameUBO{};
    ubo.camToWorld = glm::inverse(view);
    ubo.viewParams = vec4(0, 0, getCameraFocalLength(), 0);
    int flags = (gfx.ao ? FLAG_AO : 0) | (drawGrid ? FLAG_GRID : 0) | (gfx.lightShadows ? FLAG_LIGHT_SHADOWS : 0);
    ubo.counts = glm::ivec4((int)gpuObjectCache.size(), (int)gpuLightCache.size(), currentShadingMode, flags);
    ubo.quality = glm::ivec4(gfx.maxSteps, gfx.shadowMode, gfx.shadowSteps, gfx.bounces);
    ubo.sunDir = vec4(sunDirection(world), world.sunIntensity);
    ubo.sunColor = vec4(world.sunColor, world.ambient);
    ubo.skyTop = vec4(world.skyTop, 0);
    ubo.skyHorizon = vec4(world.skyHorizon, 0);
    for (size_t i = 0; i < gpuLightCache.size(); i++) {
        ubo.lightPos[i] = gpuLightCache[i].posRange;
        ubo.lightCol[i] = gpuLightCache[i].color;
    }

    // Хеш всего, от чего зависит картинка. Не изменился — кадр берём готовый.
    uint64_t hash = fnv1a(&ubo, sizeof(ubo));
    if (!gpuObjectCache.empty()) hash = fnv1a(gpuObjectCache.data(), gpuObjectCache.size() * sizeof(GpuObject), hash);
    int dims[2] = { vw, vh };
    hash = fnv1a(dims, sizeof(dims), hash);

    bool changed = (hash != lastSceneHash) || forceRender || !offscreenReady;
    lastSceneHash = hash;

    float staticScale = std::clamp(gfx.renderScale, 0.2f, 1.0f);
    float moveScale = std::min(gfx.autoScale ? dynamicScale : gfx.interactiveScale, staticScale);
    moveScale = std::max(0.2f, std::round(moveScale * 20.0f) / 20.0f);   // Шаг 5%, чтобы картинка не «дышала»

    if (continuous) {
        plan.render = true;
        plan.scale = staticScale;
    } else if (changed) {
        // Первое изменение после покоя (клик, правка в инспекторе) рисуем сразу начисто;
        // если изменения идут подряд — это движение, включаем облегчённый режим
        bool moving = stillFrames < 3 && !forceRender;
        stillFrames = 0;
        plan.render = true;
        plan.interactive = moving;
        plan.scale = moving ? moveScale : staticScale;
    } else {
        stillFrames++;
        // Движение закончилось — дорисовываем кадр в полном качестве
        if (lastRenderInteractive && stillFrames >= 3) { plan.render = true; plan.scale = staticScale; }
    }
    forceRender = false;

    if (plan.render) {
        plan.renderW = std::clamp((uint32_t)std::lround(vw * plan.scale), 8u, (uint32_t)vw);
        plan.renderH = std::clamp((uint32_t)std::lround(vh * plan.scale), 8u, (uint32_t)vh);
        ubo.viewParams.x = (float)plan.renderW;
        ubo.viewParams.y = (float)plan.renderH;
        if (plan.interactive && gfx.simplifyOnInteract && currentShadingMode > SHADING_SOLID) ubo.counts.z = SHADING_SOLID;
    }
    return plan;
}

void Renderer::drawFrame(GLFWwindow* window, bool& framebufferResized) {
    checkShaderHotReload();
    if (gfxSwapchainDirty) { gfxSwapchainDirty = false; recreateSwapchain(window); }

    if (uploadedVolumeVersion != volumeDataVersion) uploadVolumes();
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    readTimestamps(currentFrame);

    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(window); return; }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) vkCheck(res, "vkAcquireNextImageKHR");
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    FrameUBO ubo;
    FramePlan plan = planFrame(ubo);

    // Без замеров GPU подгоняем масштаб по времени кадра на CPU
    double now = glfwGetTime();
    if (plan.render && plan.interactive && gfx.autoScale && !queryPool && lastRenderInteractive) {
        double dt = now - lastFrameTime, target = 1.0 / gfx.targetFps;
        if (dt > target * 1.15) dynamicScale = std::max(0.2f, dynamicScale * 0.93f);
        else if (dt < target * 1.05) dynamicScale = std::min(1.0f, dynamicScale * 1.02f);
    }
    lastFrameTime = now;

    renderStats.renderedThisFrame = plan.render;
    if (plan.render) {
        memcpy(uboMapped[currentFrame], &ubo, sizeof(ubo));
        if (!gpuObjectCache.empty()) memcpy(sceneMapped[currentFrame], gpuObjectCache.data(), gpuObjectCache.size() * sizeof(GpuObject));
        lastRenderW = plan.renderW; lastRenderH = plan.renderH;
        lastRenderInteractive = plan.interactive;
        slotInteractive[currentFrame] = plan.interactive;
        slotScale[currentFrame] = plan.scale;
        renderStats.renderW = (int)plan.renderW; renderStats.renderH = (int)plan.renderH;
        renderStats.framesRendered++;
    } else {
        renderStats.framesReused++;
    }

    recordCommandBuffer(commandBuffers[currentFrame], imageIndex, plan);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &imageAvailable[currentFrame]; si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1; si.pCommandBuffers = &commandBuffers[currentFrame];
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &renderFinished[imageIndex];
    vkCheck(vkQueueSubmit(queue, 1, &si, inFlightFences[currentFrame]), "vkQueueSubmit");

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderFinished[imageIndex];
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain; pi.pImageIndices = &imageIndex;
    res = vkQueuePresentKHR(queue, &pi);
    currentFrame = (currentFrame + 1) % FRAMES_IN_FLIGHT;
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized) { framebufferResized = false; recreateSwapchain(window); }
}

void Renderer::recordCommandBuffer(VkCommandBuffer cb, uint32_t imageIndex, const FramePlan& plan) {
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    if (plan.render) {
        // SDF-кадр: compute пишет в offscreen-картинки
        VkImage targets[2] = { sdfColorImage, sdfDepthImage };
        VkImageMemoryBarrier barriers[2] = {};
        for (int i = 0; i < 2; i++) {
            barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].srcAccessMask = offscreenReady ? VK_ACCESS_SHADER_READ_BIT : 0;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            // Содержимое всё равно перезаписывается целиком, сохранять его не нужно
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image = targets[i];
            barriers[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        }
        vkCmdPipelineBarrier(cb, offscreenReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

        if (queryPool) {
            vkCmdResetQueryPool(cb, queryPool, currentFrame * 2, 2);
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, currentFrame * 2);
        }

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout, 0, 1, &computeSets[currentFrame], 0, nullptr);
        vkCmdDispatch(cb, (plan.renderW + 7) / 8, (plan.renderH + 7) / 8, 1);

        if (queryPool) {
            vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, currentFrame * 2 + 1);
            slotQueryValid[currentFrame] = true;
        }

        for (int i = 0; i < 2; i++) {
            barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL; barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
        offscreenReady = true;
    }

    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = renderPass; info.framebuffer = framebuffers[imageIndex]; info.renderArea.extent = swapchainExtent;
    VkClearValue clearValues[2]; clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}}; clearValues[1].depthStencil = {1.0f, 0};
    info.clearValueCount = 2; info.pClearValues = clearValues;
    vkCmdBeginRenderPass(cb, &info, VK_SUBPASS_CONTENTS_INLINE);

    if (plan.hasViewport && offscreenReady && lastRenderW > 0) {
        const VkRect2D& vp = plan.viewport;
        VkViewport fullViewport{0.0f, 0.0f, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &fullViewport);
        vkCmdSetScissor(cb, 0, 1, &vp);

        // Растягиваем SDF-кадр на вьюпорт и переносим его глубину
        CompositePush cp;
        cp.rect = vec4((float)vp.offset.x, (float)vp.offset.y, (float)vp.extent.width, (float)vp.extent.height);
        vec2 texSize((float)swapchainExtent.width, (float)swapchainExtent.height);
        vec2 used((float)lastRenderW, (float)lastRenderH);
        cp.uvScale = vec4(used / texSize, (used - vec2(0.5f)) / texSize);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, compositeLayout, 0, 1, &compositeSet, 0, nullptr);
        vkCmdPushConstants(cb, compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cp), &cp);
        vkCmdDraw(cb, 3, 1, 0, 0);

        // Полигональные меши поверх, с тестом глубины против SDF
        bool anyMesh = false;
        for (const auto& node : sceneGraph) if (node.meshIdx >= 0 && node.worldActive) { anyMesh = true; break; }
        if (anyMesh) {
            VkViewport meshViewport{(float)vp.offset.x, (float)vp.offset.y, (float)vp.extent.width, (float)vp.extent.height, 0.0f, 1.0f};
            vkCmdSetViewport(cb, 0, 1, &meshViewport);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

            CameraBasis camBasis = getCameraBasis();
            mat4 view = glm::lookAt(camBasis.pos, camBasis.pos + camBasis.fwd, camBasis.up);
            float fovY = 2.0f * std::atan(1.0f / getCameraFocalLength());
            mat4 proj = glm::perspective(fovY, (float)vp.extent.width / (float)vp.extent.height, NEAR_PLANE, FAR_PLANE);
            proj[1][1] *= -1; // Vulkan Y inversion
            mat4 viewProj = proj * view;
            vec3 sun = sunDirection(world);

            for (int idx = 0; idx < (int)sceneGraph.size(); idx++) {
                const auto& node = sceneGraph[idx];
                if (node.meshIdx < 0 || node.meshIdx >= (int)meshCache.size() || !node.worldActive) continue;
                const auto& mesh = meshCache[node.meshIdx];
                if (!mesh.isLoaded) continue;
                mat4 model = calculateWorldMatrix(idx);
                mat3 normalMat = glm::transpose(glm::inverse(mat3(model)));
                MeshPush mp;
                mp.mvp = viewProj * model;
                mp.normalRow0 = vec4(normalMat[0][0], normalMat[1][0], normalMat[2][0], sun.x);
                mp.normalRow1 = vec4(normalMat[0][1], normalMat[1][1], normalMat[2][1], sun.y);
                mp.normalRow2 = vec4(normalMat[0][2], normalMat[1][2], normalMat[2][2], sun.z);
                mp.color = vec4(vec3(node.gpuData.color), mesh.showChecker ? 1.0f : 0.0f);   // w: рисовать шахматку по UV
                vkCmdPushConstants(cb, meshLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush), &mp);
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cb, 0, 1, &mesh.vertexBuffer.buffer, offsets);
                vkCmdBindIndexBuffer(cb, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, mesh.indexBuffer.count, 1, 0, 0, 0);
            }
        }
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
}

void Renderer::initImGui(GLFWwindow* window) {
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 } };
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; pool_info.maxSets = 256;
    pool_info.poolSizeCount = 1; pool_info.pPoolSizes = pool_sizes;
    vkCheck(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiDescriptorPool), "vkCreateDescriptorPool");

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance; init_info.PhysicalDevice = physicalDevice; init_info.Device = device;
    init_info.QueueFamily = queueFamily; init_info.Queue = queue; init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = 2; init_info.ImageCount = (uint32_t)swapchainImages.size(); init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&init_info, renderPass)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");

    VkCommandBuffer cb = beginOneShot();
    ImGui_ImplVulkan_CreateFontsTexture(cb);
    endOneShot(cb);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void Renderer::cleanupSwapchain() {
    vkDestroyImageView(device, sdfColorView, nullptr); vmaDestroyImage(allocator, sdfColorImage, sdfColorAllocation);
    vkDestroyImageView(device, sdfDepthView, nullptr); vmaDestroyImage(allocator, sdfDepthImage, sdfDepthAllocation);
    vkDestroyImageView(device, depthImageView, nullptr); vmaDestroyImage(allocator, depthImage, depthImageAllocation);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
    for (auto sem : renderFinished) vkDestroySemaphore(device, sem, nullptr);
    framebuffers.clear(); swapchainImageViews.clear(); renderFinished.clear();
}

void Renderer::recreateSwapchain(GLFWwindow* window) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) { glfwWaitEvents(); glfwGetFramebufferSize(window, &width, &height); }
    vkDeviceWaitIdle(device);
    cleanupSwapchain();
    createSwapchain(window);
    createImageViews();
    createDepthResources();
    createOffscreenResources();
    createFramebuffers();
    updateImageDescriptors();
    ImGui_ImplVulkan_SetMinImageCount(2);
}

// =================================================================
// --- Resources ---
// =================================================================
bool Renderer::uploadMesh(const OBJModel& model, MeshData& out) {
    if (model.vertices.empty() || model.indices.empty()) return false;

    VkDeviceSize vSize = sizeof(Vertex) * model.vertices.size();
    createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, out.vertexBuffer.buffer, out.vertexBuffer.allocation);
    void* vData; vmaMapMemory(allocator, out.vertexBuffer.allocation, &vData);
    memcpy(vData, model.vertices.data(), (size_t)vSize);
    vmaUnmapMemory(allocator, out.vertexBuffer.allocation);
    out.vertexBuffer.count = (uint32_t)model.vertices.size();

    VkDeviceSize iSize = sizeof(uint32_t) * model.indices.size();
    createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, out.indexBuffer.buffer, out.indexBuffer.allocation);
    void* iData; vmaMapMemory(allocator, out.indexBuffer.allocation, &iData);
    memcpy(iData, model.indices.data(), (size_t)iSize);
    vmaUnmapMemory(allocator, out.indexBuffer.allocation);
    out.indexBuffer.count = (uint32_t)model.indices.size();

    out.boundsMin = vec3(1e10f); out.boundsMax = vec3(-1e10f);
    for (const auto& v : model.vertices) { out.boundsMin = glm::min(out.boundsMin, v.pos); out.boundsMax = glm::max(out.boundsMax, v.pos); }
    out.vertices = model.vertices;
    out.isLoaded = true;
    return true;
}

void Renderer::updateMeshVertices(MeshData& mesh) {
    if (!mesh.isLoaded || mesh.vertices.size() != mesh.vertexBuffer.count) return;
    vkDeviceWaitIdle(device);   // Буфер может читать кадр, который ещё в работе
    void* data;
    vmaMapMemory(allocator, mesh.vertexBuffer.allocation, &data);
    memcpy(data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    vmaFlushAllocation(allocator, mesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(allocator, mesh.vertexBuffer.allocation);
}

std::vector<uint8_t> Renderer::captureScreenshot(int& outW, int& outH) {
    outW = outH = 0;
    if (!offscreenReady || lastRenderW == 0) return {};
    vkDeviceWaitIdle(device);

    VkDeviceSize imageSize = (VkDeviceSize)lastRenderW * lastRenderH * 4;
    VkBuffer dstBuffer; VmaAllocation dstAllocation;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, dstBuffer, dstAllocation);

    VkCommandBuffer cb = beginOneShot();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = sdfColorImage; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {lastRenderW, lastRenderH, 1};
    vkCmdCopyImageToBuffer(cb, sdfColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endOneShot(cb);

    std::vector<uint8_t> pixels(imageSize);
    void* data; vmaMapMemory(allocator, dstAllocation, &data);
    vmaInvalidateAllocation(allocator, dstAllocation, 0, VK_WHOLE_SIZE);
    memcpy(pixels.data(), data, (size_t)imageSize);
    vmaUnmapMemory(allocator, dstAllocation);
    vmaDestroyBuffer(allocator, dstBuffer, dstAllocation);
    outW = (int)lastRenderW; outH = (int)lastRenderH;
    return pixels;
}

void Renderer::setupInternalLogo(GLFWwindow* window) {
    auto generateLogoPixels = [](int width, int height) {
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
    };
    int w = 64, h = 64; std::vector<unsigned char> pixels = generateLogoPixels(w, h);
    GLFWimage images[1]; images[0].width = w; images[0].height = h; images[0].pixels = const_cast<unsigned char*>(pixels.data()); glfwSetWindowIcon(window, 1, images);
    g_AppLogoID = createTexture(pixels.data(), w, h);
}

Renderer::UiTexture Renderer::createTextureInternal(const uint8_t* rgba, int w, int h) {
    UiTexture tex;
    VkDeviceSize imageSize = (VkDeviceSize)w * h * 4;
    VkBuffer stagingBuffer; VmaAllocation stagingAllocation;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAllocation);
    void* data; vmaMapMemory(allocator, stagingAllocation, &data); memcpy(data, rgba, (size_t)imageSize); vmaUnmapMemory(allocator, stagingAllocation);

    createImage((uint32_t)w, (uint32_t)h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, tex.image, tex.allocation);

    VkCommandBuffer cb = beginOneShot();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cb, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endOneShot(cb);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    tex.view = createImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    tex.id = ImGui_ImplVulkan_AddTexture(uiSampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return tex;
}

void* Renderer::createTexture(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return nullptr;
    uiTextures.push_back(createTextureInternal(rgba, w, h));
    return uiTextures.back().id;
}

void Renderer::destroyTexture(void* textureId) {
    for (size_t i = 0; i < uiTextures.size(); i++) {
        if (uiTextures[i].id != textureId) continue;
        vkDeviceWaitIdle(device);
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)uiTextures[i].id);
        vkDestroyImageView(device, uiTextures[i].view, nullptr);
        vmaDestroyImage(allocator, uiTextures[i].image, uiTextures[i].allocation);
        uiTextures.erase(uiTextures.begin() + i);
        return;
    }
}
