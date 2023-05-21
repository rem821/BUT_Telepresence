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
#include "VulkanBuffer.h"
#include "VulkanFrameInfo.h"

namespace VulkanEngine {

    class VulkanGraphicsContext {
    public:
        explicit VulkanGraphicsContext(const std::shared_ptr<Options> &options);

        std::vector<std::string> GetInstanceExtensions() const {
            return {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
        };

        const XrBaseInStructure* GetGraphicsBinding() const {
            return reinterpret_cast<const XrBaseInStructure*>(&graphicsBinding_);
        }

        void InitializeDevice(const XrInstance &instance, const XrSystemId &systemId);

        void InitializeRendering(const XrInstance &instance, const XrSystemId &systemId, const XrSession &session);

        void RenderView(XrCompositionLayerProjectionView &layerView, Geometry::DisplayType display, const void *image);

    private:
        XrGraphicsBindingVulkan2KHR graphicsBinding_{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
        XrViewConfigurationType viewConfigurationType_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};

        std::unique_ptr<VulkanDevice> vulkanDevice_;
        std::unique_ptr<VulkanRenderer> vulkanRenderer_;
        std::unique_ptr<VulkanDescriptorPool> globalPool_;
        std::unique_ptr<VulkanDescriptorSetLayout> globalSetLayout_;
        std::vector<std::unique_ptr<VulkanRenderSystem>> renderSystem_;

        std::vector<std::unique_ptr<VulkanBuffer>> uboBuffers{2};
        std::vector<VkDescriptorSet> globalDescriptorSets{2};
    };
}