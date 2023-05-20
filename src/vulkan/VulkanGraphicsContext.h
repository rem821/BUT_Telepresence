//
// Created by standa on 20.05.23.
//
#pragma once

#include "../pch.h"
#include "../options.h"

#include "VulkanDevice.h"
#include "VulkanRenderer.h"
#include "VulkanDescriptors.h"
#include "VulkanRenderSystem.h"

#include "../xr_linear.h"

namespace VulkanEngine {

    class VulkanGraphicsContext {
    public:
        explicit VulkanGraphicsContext(const std::shared_ptr<Options> &options);

        std::vector<std::string> GetInstanceExtensions() const {
            return {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
        };

        void InitializeDevice(XrInstance instance, XrSystemId systemId);

        void InitializeRendering(XrInstance instance, XrSystemId systemId, XrSession session);

        void RenderView(const XrCompositionLayerProjectionView &layerView,
                        const XrSwapchainImageBaseHeader *swapchainImage,
                        int64_t swapchainFormat, const void *image);

    private:
        XrGraphicsBindingVulkan2KHR graphicsBinding_{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
        XrViewConfigurationType viewConfigurationType_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};

        std::unique_ptr<VulkanDevice> vulkanDevice_;
        std::unique_ptr<VulkanRenderer> vulkanRenderer_;
        std::unique_ptr<VulkanDescriptorPool> globalPool_;
        std::unique_ptr<VulkanDescriptorSetLayout> globalSetLayout_;
        std::vector<std::unique_ptr<VulkanRenderSystem>> renderSystem_;
        VkSemaphore m_vkDrawDone{VK_NULL_HANDLE};

        std::array<float, 4> clearColor_;
    };
}