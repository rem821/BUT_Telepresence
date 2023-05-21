//
// Created by Stanislav Svědiroh on 13.06.2022.
//

#include "VulkanSwapChain.h"

namespace VulkanEngine {


    VulkanSwapChain::VulkanSwapChain(const VulkanDevice &engineDevice, const XrSession &xrSession,
                                     const XrViewConfigurationType &viewConfigurationType)
            : device_{engineDevice}, xrSession_(xrSession),
              viewConfigurationType_(viewConfigurationType) {
        Init();
    }

    void VulkanSwapChain::Init() {
        CreateSwapChain();
        CreateImageViews();
        CreateRenderPass();
        CreateDepthResources();
        CreateFrameBuffers();
        CreateSyncObjects();
    }


    VulkanSwapChain::~VulkanSwapChain() {
        for (auto imageContext: swapchainImageContexts_) {
            for (auto imageView: imageContext.colorImageViews) {
                vkDestroyImageView(device_.GetDevice(), imageView, nullptr);
            }
            imageContext.colorImageViews.clear();

            for (auto depthImageView: imageContext.depthImageViews) {
                vkDestroyImageView(device_.GetDevice(), depthImageView, nullptr);

            }
            vkDestroyImage(device_.GetDevice(), imageContext.depthImage, nullptr);
            vkFreeMemory(device_.GetDevice(), imageContext.depthImageMemory, nullptr);

            for (auto frameBuffer: imageContext.frameBuffers) {
                vkDestroyFramebuffer(device_.GetDevice(), frameBuffer, nullptr);
            }

            vkDestroyRenderPass(device_.GetDevice(), imageContext.renderPass, nullptr);
        }

        vkDestroyFence(device_.GetDevice(), execFence_, nullptr);
    }

    void VulkanSwapChain::AcquireNextImage(Geometry::DisplayType display) {
        vkWaitForFences(device_.GetDevice(), 1, &execFence_, VK_TRUE, std::numeric_limits<uint64_t>::max());

        // Each view has a separate swapchain which is acquired, rendered to, and released.
        currentSwapchain_ = swapchains_[display];
        currentDisplay_ = display;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

        CHECK_XRCMD(xrAcquireSwapchainImage(currentSwapchain_.handle, &acquireInfo, &currentSwapchainImageIndex_))

        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        CHECK_XRCMD(xrWaitSwapchainImage(currentSwapchain_.handle, &waitInfo))
    }

    VkResult VulkanSwapChain::SubmitCommandBuffers(const VkCommandBuffer *buffers) {
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = buffers;

        vkResetFences(device_.GetDevice(), 1, &execFence_);
        CORE_ASSERT(vkQueueSubmit(device_.GraphicsQueue(), 1, &submitInfo, execFence_) == VK_SUCCESS,
                    "Failed to submit draw command buffer!")

        return VK_SUCCESS;
    }

    void VulkanSwapChain::CreateSwapChain() {
        // Query and cache view configuration views
        uint32_t viewCount;
        CHECK_XRCMD(
                xrEnumerateViewConfigurationViews(device_.GetXrInstance(), device_.GetXrSystemId(), viewConfigurationType_, 0, &viewCount, nullptr))
        configViews_.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        CHECK_XRCMD(xrEnumerateViewConfigurationViews(device_.GetXrInstance(), device_.GetXrSystemId(), viewConfigurationType_, viewCount, &viewCount,
                                                      configViews_.data()))

        if (viewCount > 0) {
            uint32_t swapchainFormatCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(xrSession_, 0, &swapchainFormatCount, nullptr))
            std::vector<int64_t> swapchainFormats(swapchainFormatCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(xrSession_, swapchainFormatCount, &swapchainFormatCount, swapchainFormats.data()))
            // List of supported color swapchain formats.
            constexpr int64_t SupportedColorSwapchainFormats[] = {VK_FORMAT_B8G8R8A8_SRGB,
                                                                  VK_FORMAT_R8G8B8A8_SRGB,
                                                                  VK_FORMAT_B8G8R8A8_UNORM,
                                                                  VK_FORMAT_R8G8B8A8_UNORM};

            auto swapchainFormatIt = std::find_first_of(swapchainFormats.begin(), swapchainFormats.end(),
                                                        std::begin(SupportedColorSwapchainFormats),
                                                        std::end(SupportedColorSwapchainFormats));
            if (swapchainFormatIt == swapchainFormats.end()) {
                THROW("No runtime swapchain format supported for color swapchain")
            }

            swapchainImageFormat_ = (VkFormat) *swapchainFormatIt;
            swapchainDepthFormat_ = VK_FORMAT_D32_SFLOAT;
            swapchainExtent_ = {configViews_[0].recommendedImageRectWidth,
                                configViews_[0].recommendedImageRectHeight};

            // Print swapchain formats and the selected one
            {
                std::string swapchainFormatsString;
                for (int64_t format: swapchainFormats) {
                    const bool selected = format == swapchainImageFormat_;
                    swapchainFormatsString += " ";
                    if (selected) {
                        swapchainFormatsString += "[";
                    }
                    swapchainFormatsString += std::to_string(format);
                    if (selected) {
                        swapchainFormatsString += "]";
                    }
                }
                LOG_INFO("Swapchain Formats: %s", swapchainFormatsString.c_str());
            }

            // Create a swapchain for each view.
            swapchainImageContexts_.resize(viewCount);
            for (uint32_t i = 0; i < viewCount; i++) {
                const XrViewConfigurationView &vp = configViews_[i];
                LOG_INFO("Creating swapchain for view %d with dimensions Width=%d Height=%d SampleCount=%d", i, vp.recommendedImageRectHeight,
                         vp.recommendedImageRectHeight, vp.recommendedSwapchainSampleCount);

                // Create a swapchain
                XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                swapchainCreateInfo.arraySize = 1;
                swapchainCreateInfo.format = swapchainImageFormat_;
                swapchainCreateInfo.width = vp.recommendedImageRectWidth;
                swapchainCreateInfo.height = vp.recommendedImageRectHeight;
                swapchainCreateInfo.mipCount = 1;
                swapchainCreateInfo.faceCount = 1;
                swapchainCreateInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

                Swapchain swapchain{};
                swapchain.width = static_cast<int32_t>(swapchainCreateInfo.width);
                swapchain.height = static_cast<int32_t>(swapchainCreateInfo.height);
                CHECK_XRCMD(xrCreateSwapchain(xrSession_, &swapchainCreateInfo, &swapchain.handle))

                swapchains_.push_back(swapchain);
                uint32_t imageCount;
                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr))

                swapchainImageContexts_[i].xrSwapchainImages.resize(imageCount);
                std::vector<XrSwapchainImageBaseHeader *> swapchainImageBases(imageCount);
                for (int j = 0; j < imageCount; j++) {
                    swapchainImageContexts_[i].xrSwapchainImages[j] = {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR};
                    swapchainImageBases[j] = {reinterpret_cast<XrSwapchainImageBaseHeader *>(&swapchainImageContexts_[i].xrSwapchainImages[j])};
                }

                CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, swapchainImageBases[0]))
                swapchainImagePairs_.insert(std::make_pair(swapchain.handle, std::move(swapchainImageBases)));
            }
        }
    }

    void VulkanSwapChain::CreateImageViews() {
        for (size_t i = 0; i < swapchainImageContexts_.size(); i++) {
            swapchainImageContexts_[i].colorImages.resize(swapchainImageContexts_[i].xrSwapchainImages.size());
            swapchainImageContexts_[i].colorImageViews.resize(swapchainImageContexts_[i].xrSwapchainImages.size());
            for (size_t j = 0; j < swapchainImageContexts_[i].xrSwapchainImages.size(); j++) {
                swapchainImageContexts_[i].colorImages[j] = swapchainImageContexts_[i].xrSwapchainImages[j].image;

                if (swapchainImageContexts_[i].colorImages[j] != VK_NULL_HANDLE) {
                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = swapchainImageContexts_[i].colorImages[j];
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = swapchainImageFormat_;
                    viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
                    viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
                    viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
                    viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.baseMipLevel = 0;
                    viewInfo.subresourceRange.levelCount = 1;
                    viewInfo.subresourceRange.baseArrayLayer = 0;
                    viewInfo.subresourceRange.layerCount = 1;

                    CORE_ASSERT(
                            vkCreateImageView(device_.GetDevice(), &viewInfo, nullptr, &swapchainImageContexts_[i].colorImageViews[j]) == VK_SUCCESS,
                            "Failed to create color image view!")
                }
            }
        }
    }

    void VulkanSwapChain::CreateDepthResources() {
        VkExtent2D swapChainExt = GetSwapChainExtent();

        for (size_t i = 0; i < swapchainImageContexts_.size(); i++) {
            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapChainExt.width;
            imageInfo.extent.height = swapChainExt.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = swapchainDepthFormat_;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = 0;

            device_.CreateImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, swapchainImageContexts_[i].depthImage,
                                        swapchainImageContexts_[i].depthImageMemory);

            swapchainImageContexts_[i].depthImageViews.resize(swapchainImageContexts_[i].xrSwapchainImages.size());
            for (int j = 0; j < swapchainImageContexts_[i].xrSwapchainImages.size(); j++) {
                VkImageViewCreateInfo viewInfo = {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = swapchainImageContexts_[i].depthImage;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = swapchainDepthFormat_;
                viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
                viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
                viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
                viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                CORE_ASSERT(vkCreateImageView(device_.GetDevice(), &viewInfo, nullptr, &swapchainImageContexts_[i].depthImageViews[j]) == VK_SUCCESS,
                            "Failed to create depth image view!")
            }
        }
    }

    void VulkanSwapChain::CreateRenderPass() {
        for (int i = 0; i < swapchainImageContexts_.size(); i++) {
            VkAttachmentDescription depthAttachment = {};
            depthAttachment.format = swapchainDepthFormat_;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef = {};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentDescription colorAttachment = {};
            colorAttachment.format = GetSwapChainImageFormat();
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorAttachmentRef = {};
            colorAttachmentRef.attachment = 0;
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorAttachmentRef;
            subpass.pDepthStencilAttachment = &depthAttachmentRef;

            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.srcAccessMask = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstSubpass = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
            VkRenderPassCreateInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpass;
            renderPassInfo.dependencyCount = 1;
            renderPassInfo.pDependencies = &dependency;

            CORE_ASSERT(vkCreateRenderPass(device_.GetDevice(), &renderPassInfo, nullptr, &swapchainImageContexts_[i].renderPass) == VK_SUCCESS,
                        "Failed to create render pass!")
        }
    }

    void VulkanSwapChain::CreateFrameBuffers() {
        for (size_t i = 0; i < swapchainImageContexts_.size(); i++) {
            swapchainImageContexts_[i].frameBuffers.resize(swapchainImageContexts_[i].xrSwapchainImages.size());

            for (size_t j = 0; j < swapchainImageContexts_[i].xrSwapchainImages.size(); j++) {
                std::array<VkImageView, 2> attachments = {swapchainImageContexts_[i].colorImageViews[j],
                                                          swapchainImageContexts_[i].depthImageViews[j]};

                VkExtent2D swapChainExt = GetSwapChainExtent();

                VkFramebufferCreateInfo framebufferInfo = {};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = swapchainImageContexts_[i].renderPass;
                framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
                framebufferInfo.pAttachments = attachments.data();
                framebufferInfo.width = swapChainExt.width;
                framebufferInfo.height = swapChainExt.height;
                framebufferInfo.layers = 1;

                CORE_ASSERT(vkCreateFramebuffer(device_.GetDevice(), &framebufferInfo, nullptr, &swapchainImageContexts_[i].frameBuffers[j]) ==
                            VK_SUCCESS, "Failed to create framebuffer!")
            }
        }
    }

    void VulkanSwapChain::CreateSyncObjects() {
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        CORE_ASSERT(vkCreateFence(device_.GetDevice(), &fenceInfo, nullptr, &execFence_) == VK_SUCCESS,
                    "Failed to create synchronization objects for a frame!")
    }
}