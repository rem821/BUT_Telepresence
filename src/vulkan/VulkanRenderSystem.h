//
// Created by standa on 10.5.23.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"
#include "VulkanPipeline.h"

#include "../xr_linear.h"

namespace VulkanEngine {

    struct SimplePushConstants {
        XrMatrix4x4f modelMatrix{1.f};
        XrMatrix4x4f normalMatrix{1.f};
    };

    class VulkanRenderSystem {
    public:
        VulkanRenderSystem(VulkanDevice &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout, VkPolygonMode polygonMode, VkCullModeFlagBits cullMode);

        ~VulkanRenderSystem();

        VulkanRenderSystem(const VulkanRenderSystem &) = delete;

        VulkanRenderSystem &operator=(const VulkanRenderSystem &) = delete;

        void RenderGameObjects();

    private:
        void CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout);

        void CreatePipeline(VkRenderPass renderPass, VkPolygonMode polygonMode, VkCullModeFlagBits cullMode);

        VulkanDevice &engineDevice_;

        bool isWireFrame_ = false;

        std::unique_ptr<VulkanPipeline> enginePipeline_;
        VkPipelineLayout pipelineLayout_{};
    };
}