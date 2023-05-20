//
// Created by standa on 4.3.23.
//
#include "VulkanDevice.h"

namespace VulkanEngine {

    VulkanDevice::VulkanDevice(const XrInstance &xrInstance, const XrSystemId &systemId, bool enableValidationLayers)
            : xrInstance_(xrInstance), xrSystemId_(systemId), enableValidationLayers_(enableValidationLayers) {
        CreateInstance();
        SetupDebugMessenger();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateCommandPool();
    }

    VulkanDevice::~VulkanDevice() = default;

    void VulkanDevice::CreateInstance() {
        CORE_ASSERT(enableValidationLayers_ && CheckValidationLayerSupport(), "Validation layers requested, but not available!")

        XrGraphicsRequirementsVulkan2KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};

        PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanGraphicsRequirements2KHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(xrInstance_, "xrGetVulkanGraphicsRequirements2KHR",
                                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnGetVulkanGraphicsRequirements2KHR)))
        CHECK_XRCMD(pfnGetVulkanGraphicsRequirements2KHR(xrInstance_, xrSystemId_, &graphicsRequirements))

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = appName_.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = engineName_.c_str();
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = GetRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (enableValidationLayers_) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
            createInfo.ppEnabledLayerNames = validationLayers_.data();
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        XrVulkanInstanceCreateInfoKHR xrCreateInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        xrCreateInfo.systemId = xrSystemId_;
        xrCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
        xrCreateInfo.vulkanCreateInfo = &createInfo;
        xrCreateInfo.vulkanAllocator = nullptr;

        PFN_xrCreateVulkanInstanceKHR pfnCreateVulkanInstanceKHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(xrInstance_, "xrCreateVulkanInstanceKHR",
                                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnCreateVulkanInstanceKHR)))

        VkResult err;
        CHECK_XRCMD(pfnCreateVulkanInstanceKHR(xrInstance_, &xrCreateInfo, &instance_, &err))
        CORE_ASSERT(err == VK_SUCCESS, "Failed to create the Vulkan Instance!")
    }

    void
    VulkanDevice::PickPhysicalDevice() {
        XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
        deviceGetInfo.systemId = xrSystemId_;
        deviceGetInfo.vulkanInstance = instance_;

        PFN_xrGetVulkanGraphicsDevice2KHR pfnGetVulkanGraphicsDevice2KHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(xrInstance_, "xrGetVulkanGraphicsDevice2KHR",
                                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnGetVulkanGraphicsDevice2KHR)))

        CHECK_XRCMD(pfnGetVulkanGraphicsDevice2KHR(xrInstance_, &deviceGetInfo, &physicalDevice_))
    }

    void
    VulkanDevice::CreateLogicalDevice() {
        QueueFamilyIndices indices = FindQueueFamilies(physicalDevice_);

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfo.queueFamilyIndex = indices.graphicsFamily;

        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions_.data();
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = nullptr;

        XrVulkanDeviceCreateInfoKHR deviceCreateInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        deviceCreateInfo.systemId = xrSystemId_;
        deviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
        deviceCreateInfo.vulkanCreateInfo = &createInfo;
        deviceCreateInfo.vulkanPhysicalDevice = physicalDevice_;
        deviceCreateInfo.vulkanAllocator = nullptr;

        PFN_xrCreateVulkanDeviceKHR pfnCreateVulkanDeviceKHR = nullptr;
        CHECK_XRCMD(xrGetInstanceProcAddr(xrInstance_, "xrCreateVulkanDeviceKHR",
                                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnCreateVulkanDeviceKHR)))
        VkResult err;
        CHECK_XRCMD(pfnCreateVulkanDeviceKHR(xrInstance_, &deviceCreateInfo, &device_, &err));
        CORE_ASSERT(err == VK_SUCCESS, "Failed to create the Vulkan Logical Device!")

        vkGetDeviceQueue(device_, indices.graphicsFamily, 0, &graphicsQueue_);
    }

    void VulkanDevice::CreateCommandPool() {
        QueueFamilyIndices queueFamilyIndices = FindPhysicalQueueFamilies();

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        CORE_ASSERT(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) == VK_SUCCESS,
                    "Failed to create command pool!")
    }

    bool VulkanDevice::CheckValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool layerFound = false;
        for (const char *layerName: validationLayers_) {
            for (const auto &layerProperties: availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }
        }

        return layerFound;
    }

    std::vector<const char *> VulkanDevice::GetRequiredExtensions() {

        uint32_t extensionCount = 0;
        CORE_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) == VK_SUCCESS,
                    "Failed to enumerate instance extensions!")

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        CORE_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()) == VK_SUCCESS,
                    "Failed to enumerate instance extensions!")

        std::vector<const char *> extensions;
        for (auto &extension: availableExtensions) {
            // Debug utils is optional and not always available
            if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension.extensionName) == 0) {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }

        return extensions;
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(const VkPhysicalDevice &device) const {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto &queueFamily: queueFamilies) {
            if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
                indices.graphicsFamilyHasValue = true;
            }
            i++;
        }

        return indices;
    }

    VkFormat VulkanDevice::FindSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const {
        for (VkFormat format: candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        CORE_ASSERT(0, "Failed to find supported format!")
    }

    uint32_t
    VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        CORE_ASSERT(0, "Failed to find suitable memory type!")
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL
    VulkanDevice::DebugMessageThunk(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                    void *pUserData) {
        return DebugMessage(messageSeverity, messageTypes, pCallbackData);
    }

    VkBool32 VulkanDevice::DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData) {
        std::string flagNames;
        std::string objName;

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0u) {
            flagNames += "DEBUG:";
        }
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0u) {
            flagNames += "INFO:";
        }
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u) {
            flagNames += "WARN:";
        }
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u) {
            flagNames += "ERROR:";
        }
        if ((messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0u) {
            flagNames += "PERF:";
        }

        uint64_t object = 0;
        // skip loader messages about device extensions
        if (pCallbackData->objectCount > 0) {
            auto objectType = pCallbackData->pObjects[0].objectType;
            if ((objectType == VK_OBJECT_TYPE_INSTANCE) &&
                (strncmp(pCallbackData->pMessage, "Device Extension:", 17) == 0)) {
                return VK_FALSE;
            }
            objName = VkObjectTypeToString(objectType);
            object = pCallbackData->pObjects[0].objectHandle;
            if (pCallbackData->pObjects[0].pObjectName != nullptr) {
                objName += " " + std::string(pCallbackData->pObjects[0].pObjectName);
            }
        }

        LOG_INFO("%s (%s 0x%llu) %s", flagNames.c_str(), objName.c_str(), static_cast<unsigned long long>(object),
                 pCallbackData->pMessage);

        return VK_FALSE;
    }

    void VulkanDevice::SetupDebugMessenger() {
        if (!enableValidationLayers_) return;

        vkCreateDebugUtilsMessengerEXT =
                (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        vkDestroyDebugUtilsMessengerEXT =
                (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");

        if (vkCreateDebugUtilsMessengerEXT != nullptr && vkDestroyDebugUtilsMessengerEXT != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debugInfo.messageSeverity =
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

            debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = DebugMessageThunk;
            debugInfo.pUserData = this;
            CORE_ASSERT(vkCreateDebugUtilsMessengerEXT(instance_, &debugInfo, nullptr, &debugMessenger_) == VK_SUCCESS, "Failed to create the debug messenger!")
        }
    }

    void VulkanDevice::CreateBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer &buffer,
            VkDeviceMemory &bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CORE_ASSERT(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) == VK_SUCCESS, "Failed to create buffer!")

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

        CORE_ASSERT(vkAllocateMemory(device_, &allocInfo, nullptr, &bufferMemory) == VK_SUCCESS, "Failed to allocate buffer memory!")

        vkBindBufferMemory(device_, buffer, bufferMemory, 0);
    }

    VkCommandBuffer VulkanDevice::BeginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool_;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void VulkanDevice::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);

        vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    }

    void VulkanDevice::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;  // Optional
        copyRegion.dstOffset = 0;  // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        EndSingleTimeCommands(commandBuffer);
    }

    void
    VulkanDevice::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) {
        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = layerCount;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        EndSingleTimeCommands(commandBuffer);
    }

    void VulkanDevice::CreateImageWithInfo(const VkImageCreateInfo &imageInfo, VkMemoryPropertyFlags properties, VkImage &image,
                                           VkDeviceMemory &imageMemory) const {
        CORE_ASSERT(vkCreateImage(device_, &imageInfo, nullptr, &image) == VK_SUCCESS, "Failed to create image!")

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device_, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

        CORE_ASSERT(vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) == VK_SUCCESS, "Failed to allocate image memory!")


        CORE_ASSERT(vkBindImageMemory(device_, image, imageMemory, 0) == VK_SUCCESS, "Failed to bind image memory!")
    }
}