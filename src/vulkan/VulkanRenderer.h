//
// Created by Stanislav Svědiroh on 14.06.2022.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

namespace VulkanEngine {

    class VulkanRenderer {

    public:
        VulkanRenderer(const VulkanDevice &device, const XrSession& xrSession, const XrViewConfigurationType& viewConfigurationType);

        ~VulkanRenderer();

        VulkanRenderer(const VulkanRenderer &) = delete;

        VulkanRenderer &operator=(const VulkanRenderer &) = delete;

        [[nodiscard]] VkRenderPass GetSwapChainRenderPass(Geometry::DisplayType display) const { return engineSwapChain_->GetRenderPass(display); }

        [[nodiscard]] float GetAspectRatio() const { return engineSwapChain_->GetAspectRatio(); };

        [[nodiscard]] bool IsFrameInProgress() const { return isFrameStarted_; };

        [[nodiscard]] VkCommandBuffer GetCurrentCommandBuffer() const {
            CORE_ASSERT(isFrameStarted_, "Cannot get command buffer when frame not in progress")
            return commandBuffer_;
        }

        VkCommandBuffer BeginFrame();

        void EndFrame();

        void BeginSwapChainRenderPass(VkCommandBuffer commandBuffer, Geometry::DisplayType display);

        void EndSwapChainRenderPass(VkCommandBuffer commandBuffer) const;

    private:
        void CreateCommandBuffers();

        void FreeCommandBuffers();

        void RecreateSwapChain();

        const VulkanDevice &device_;
        const XrSession& xrSession_;
        const XrViewConfigurationType& viewConfigurationType_;

        std::unique_ptr<VulkanSwapChain> engineSwapChain_;
        VkCommandBuffer commandBuffer_;

        uint32_t currentImageIndex_{};

        bool isFrameStarted_ = false;
    };
}