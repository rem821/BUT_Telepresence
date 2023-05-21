//
// Created by Stanislav Svědiroh on 13.06.2022.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"

namespace VulkanEngine {
    class VulkanSwapChain {
    public:
        struct Swapchain {
            XrSwapchain handle;
            int32_t width;
            int32_t height;
        };

        struct SwapchainImageContext {
            std::vector<XrSwapchainImageVulkan2KHR> xrSwapchainImages;
            std::vector<VkImage> colorImages{VK_NULL_HANDLE};
            VkImage depthImage{VK_NULL_HANDLE};
            VkDeviceMemory depthImageMemory{VK_NULL_HANDLE};
            std::vector<VkImageView> colorImageViews{VK_NULL_HANDLE};
            std::vector<VkImageView> depthImageViews{VK_NULL_HANDLE};
            std::vector<VkFramebuffer> frameBuffers{VK_NULL_HANDLE};
            VkRenderPass renderPass{VK_NULL_HANDLE};
        };

        VulkanSwapChain(const VulkanDevice &engineDevice, const XrSession &xrSession, const XrViewConfigurationType& viewConfigurationType);

        ~VulkanSwapChain();

        VulkanSwapChain(const VulkanSwapChain &) = delete;

        VulkanSwapChain operator=(const VulkanSwapChain &) = delete;

        VkFramebuffer GetCurrentFrameBuffer() { return swapchainImageContexts_[currentDisplay_].frameBuffers[currentSwapchainImageIndex_]; };

        VkRenderPass GetRenderPass(Geometry::DisplayType display) { return swapchainImageContexts_[display].renderPass; };

        VkRenderPass GetCurrentRenderPass() { return swapchainImageContexts_[currentDisplay_].renderPass; };

        Swapchain GetCurrentSwapChain() { return currentSwapchain_; };

        VkImageView GetCurrentImageView() { return swapchainImageContexts_[currentDisplay_].colorImageViews[currentSwapchainImageIndex_]; };

        VkFormat GetSwapChainImageFormat() { return swapchainImageFormat_; };

        VkExtent2D GetSwapChainExtent() { return swapchainExtent_; };

        [[nodiscard]] uint32_t GetWidth() const { return swapchainExtent_.width; };

        [[nodiscard]] uint32_t GetHeight() const { return swapchainExtent_.height; };

        [[nodiscard]] float GetAspectRatio() const {
            return static_cast<float>(swapchainExtent_.width) /
                   static_cast<float>(swapchainExtent_.height);
        };

        void AcquireNextImage(Geometry::DisplayType display);

        VkResult SubmitCommandBuffers(const VkCommandBuffer *buffers);

    private:
        void Init();

        void CreateSwapChain();

        void CreateImageViews();

        void CreateDepthResources();

        void CreateRenderPass();

        void CreateFrameBuffers();

        void CreateSyncObjects();

        const XrSession &xrSession_;
        const VulkanDevice &device_;

        const XrViewConfigurationType& viewConfigurationType_;
        std::vector<XrViewConfigurationView> configViews_{};
        std::vector<Swapchain> swapchains_;
        std::vector<SwapchainImageContext> swapchainImageContexts_;
        std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader *>> swapchainImagePairs_;

        VkFormat swapchainImageFormat_{};
        VkFormat swapchainDepthFormat_{};
        VkExtent2D swapchainExtent_{};

        VkFence execFence_{VK_NULL_HANDLE};

        Swapchain currentSwapchain_;
        Geometry::DisplayType currentDisplay_;
        uint32_t currentSwapchainImageIndex_;
    };
}