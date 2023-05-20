//
// Created by Stanislav Svědiroh on 04.03.2023.
//
#pragma once

#include "../pch.h"

namespace VulkanEngine {

    struct QueueFamilyIndices {
        uint32_t graphicsFamily{};
        bool graphicsFamilyHasValue = false;
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    class VulkanDevice {
    public:
        explicit VulkanDevice(const XrInstance &instance, const XrSystemId &systemId,
                              bool enableValidationLayers);

        ~VulkanDevice();

        VulkanDevice(const VulkanDevice &) = delete;

        VulkanDevice operator=(const VulkanDevice &) = delete;

        VulkanDevice(VulkanDevice &&) = delete;

        VulkanDevice &operator=(VulkanDevice &&) = delete;

        [[nodiscard]] XrInstance GetXrInstance() const { return xrInstance_; }

        [[nodiscard]] XrSystemId GetXrSystemId() const { return xrSystemId_; }

        [[nodiscard]] VkCommandPool GetCommandPool() const { return commandPool_; }

        [[nodiscard]] VkInstance GetInstance() const { return instance_; }

        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }

        [[nodiscard]] VkDevice GetDevice() const { return device_; }

        [[nodiscard]] VkSurfaceKHR Surface() const { return surface_; }

        [[nodiscard]] VkQueue GraphicsQueue() const { return graphicsQueue_; }

        [[nodiscard]] uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        [[nodiscard]] QueueFamilyIndices FindPhysicalQueueFamilies() const { return FindQueueFamilies(physicalDevice_); }

        [[nodiscard]] VkFormat
        FindSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;

        void
        CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory);

        VkCommandBuffer BeginSingleTimeCommands();

        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

        void
        CreateImageWithInfo(const VkImageCreateInfo &imageInfo, VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory) const;

    private:
        void CreateInstance();

        void PickPhysicalDevice();

        void CreateLogicalDevice();

        void CreateCommandPool();

        bool CheckValidationLayerSupport();

        [[nodiscard]] QueueFamilyIndices FindQueueFamilies(const VkPhysicalDevice &device) const;

        [[nodiscard]] static std::vector<const char *> GetRequiredExtensions();

        static VKAPI_ATTR VkBool32 VKAPI_CALL
        DebugMessageThunk(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                          VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                          const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                          void *pUserData);

        static VkBool32 DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                     VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData);

        void SetupDebugMessenger();

        static std::string VkObjectTypeToString(VkObjectType objectType) {
            std::string objName;

#define LIST_OBJECT_TYPES(_)          \
    _(UNKNOWN)                        \
    _(INSTANCE)                       \
    _(PHYSICAL_DEVICE)                \
    _(DEVICE)                         \
    _(QUEUE)                          \
    _(SEMAPHORE)                      \
    _(COMMAND_BUFFER)                 \
    _(FENCE)                          \
    _(DEVICE_MEMORY)                  \
    _(BUFFER)                         \
    _(IMAGE)                          \
    _(EVENT)                          \
    _(QUERY_POOL)                     \
    _(BUFFER_VIEW)                    \
    _(IMAGE_VIEW)                     \
    _(SHADER_MODULE)                  \
    _(PIPELINE_CACHE)                 \
    _(PIPELINE_LAYOUT)                \
    _(RENDER_PASS)                    \
    _(PIPELINE)                       \
    _(DESCRIPTOR_SET_LAYOUT)          \
    _(SAMPLER)                        \
    _(DESCRIPTOR_POOL)                \
    _(DESCRIPTOR_SET)                 \
    _(FRAMEBUFFER)                    \
    _(COMMAND_POOL)                   \
    _(SURFACE_KHR)                    \
    _(SWAPCHAIN_KHR)                  \
    _(DISPLAY_KHR)                    \
    _(DISPLAY_MODE_KHR)               \
    _(DESCRIPTOR_UPDATE_TEMPLATE_KHR) \
    _(DEBUG_UTILS_MESSENGER_EXT)

            switch (objectType) {
                default:
#define MK_OBJECT_TYPE_CASE(name) \
    case VK_OBJECT_TYPE_##name:   \
        objName = #name;          \
        break;
                LIST_OBJECT_TYPES(MK_OBJECT_TYPE_CASE)
            }

            return objName;
        }

        XrInstance xrInstance_;
        XrSystemId xrSystemId_;
        bool enableValidationLayers_;

        const std::string appName_ = "BUT_Telepresence";
        const std::string engineName_ = "VulkanEngine";
        const std::vector<const char *> validationLayers_ = {"VK_LAYER_KHRONOS_validation"};
        const std::vector<const char *> deviceExtensions_ = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT{nullptr};
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT{nullptr};
        VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};

        VkInstance instance_{};
        VkSurfaceKHR surface_{};
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_{};
        VkCommandPool commandPool_{};

        VkQueue graphicsQueue_{};
    };
}
