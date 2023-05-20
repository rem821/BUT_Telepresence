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

        VkFramebuffer GetFrameBuffer(Geometry::DisplayType display, uint32_t index) { return swapchainImageContexts_[display].frameBuffers[index]; };

        VkRenderPass GetRenderPass(Geometry::DisplayType display) { return swapchainImageContexts_[display].renderPass; };

        VkImageView GetImageView(Geometry::DisplayType display, int index) { return swapchainImageContexts_[display].colorImageViews[index]; };

        VkFormat GetSwapChainImageFormat() { return swapchainImageFormat_; };

        VkExtent2D GetSwapChainExtent() { return swapchainExtent_; };

        [[nodiscard]] uint32_t GetWidth() const { return swapchainExtent_.width; };

        [[nodiscard]] uint32_t GetHeight() const { return swapchainExtent_.height; };

        [[nodiscard]] float GetAspectRatio() const {
            return static_cast<float>(swapchainExtent_.width) /
                   static_cast<float>(swapchainExtent_.height);
        };

        VkResult AcquireNextImage(uint32_t *imageIndex);

        VkResult SubmitCommandBuffers(const VkCommandBuffer *buffers, const uint32_t *imageIndex);

    private:
        void Init();

        void CreateSwapChain();

        void CreateImageViews();

        void CreateDepthResources();

        void CreateRenderPass();

        void CreateFrameBuffers();

        void CreateSyncObjects();

        const XrSession &xrSession_;
        const XrViewConfigurationType& viewConfigurationType_;
        std::vector<XrViewConfigurationView> configViews_{};
        std::vector<Swapchain> swapchains_;
        std::vector<SwapchainImageContext> swapchainImageContexts_;
        std::map<XrSwapchain, std::vector<XrSwapchainImageBaseHeader *>> swapchainImagePairs_;

        VkFormat swapchainImageFormat_{};
        VkFormat swapchainDepthFormat_{};
        VkExtent2D swapchainExtent_{};


        const VulkanDevice &device_;

        VkSwapchainKHR swapChain_{};

        VkSemaphore imageAvailableSemaphore_;
        VkSemaphore renderFinishedSemaphore_;
        VkFence inFlightFence_;
        VkFence imageInFlight_;
        size_t currentFrame_ = 0;
    };
}