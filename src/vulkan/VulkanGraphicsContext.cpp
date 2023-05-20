#include "VulkanGraphicsContext.h"

namespace VulkanEngine {

    VulkanGraphicsContext::VulkanGraphicsContext(const std::shared_ptr<Options> &options)
            : clearColor_(options->GetBackgroundClearColor()) {
        graphicsBinding_.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
        viewConfigurationType_ = options->Parsed.ViewConfigType;
    };

    void VulkanGraphicsContext::InitializeDevice(const XrInstance& instance, const XrSystemId& systemId) {
#if defined(NDEBUG)
        vulkanDevice_ = std::make_unique<VulkanDevice>(instance, systemId, true);
#else
        vulkanDevice_ = std::make_unique<VulkanDevice>(instance, systemId, false);
#endif
        graphicsBinding_.instance = vulkanDevice_->GetInstance();
        graphicsBinding_.physicalDevice = vulkanDevice_->GetPhysicalDevice();
        graphicsBinding_.device = vulkanDevice_->GetDevice();
        graphicsBinding_.queueFamilyIndex = vulkanDevice_->FindPhysicalQueueFamilies().graphicsFamily;
        graphicsBinding_.queueIndex = 0;
    }

    void VulkanGraphicsContext::InitializeRendering(const XrInstance& instance, const XrSystemId& systemId, const XrSession& session) {
        // Read graphics properties for preferred swapchain length and logging
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        CHECK_XRCMD(xrGetSystemProperties(instance, systemId, &systemProperties))

        // Log system properties
        LOG_INFO("System Properties: Name=%s VendorId=%d", systemProperties.systemName, systemProperties.vendorId);
        LOG_INFO("System Graphics Properties: MaxWidth=%d MaxHeight=%d MaxLayers=%d",
                 systemProperties.graphicsProperties.maxSwapchainImageWidth,
                 systemProperties.graphicsProperties.maxSwapchainImageHeight,
                 systemProperties.graphicsProperties.maxLayerCount);
        LOG_INFO("System Tracking Properties: OrientationTracking=%s PositionTracking=%s",
                 systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True" : "False",
                 systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True" : "False");

        vulkanRenderer_ = std::make_unique<VulkanRenderer>(*vulkanDevice_, session, viewConfigurationType_);

        globalPool_ = VulkanDescriptorPool::Builder(*vulkanDevice_)
                .SetMaxSets(1000)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100)
                .AddPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100)
                .Build();

        globalSetLayout_ = VulkanDescriptorSetLayout::Builder(*vulkanDevice_)
                .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
                .Build();

        // Create a render system for each view
        renderSystem_.resize(2);
        for (int i = 0; i < 2; i++) {
            renderSystem_[i] = std::make_unique<VulkanRenderSystem>(*vulkanDevice_,
                                                                    vulkanRenderer_->GetSwapChainRenderPass((Geometry::DisplayType) i),
                                                                    globalSetLayout_->GetDescriptorSetLayout(), VK_POLYGON_MODE_FILL,
                                                                    VK_CULL_MODE_BACK_BIT
            );
        }
    }

    void VulkanGraphicsContext::RenderView(const XrCompositionLayerProjectionView &layerView, const XrSwapchainImageBaseHeader *swapchainImage,
                                           int64_t swapchainFormat, const void *image) {
        /*
            CHECK(layerView.subImage.imageArrayIndex == 0)  // Texture arrays not supported.

            auto swapchainContext = m_swapchainImageContextMap[swapchainImage];
            uint32_t imageIndex = swapchainContext->ImageIndex(swapchainImage);

            // XXX Should double-buffer the command buffers, for now just flush
            m_cmdBuffer.Wait();

            m_cmdBuffer.Reset();

            m_cmdBuffer.Begin();

            // Ensure depth is in the right layout
            swapchainContext->depthBuffer.TransitionLayout(&m_cmdBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            // Bind and clear eye render target
            static std::array<VkClearValue, 2> clearValues;
            clearValues[0].color.float32[0] = clearColor_[0];
            clearValues[0].color.float32[1] = clearColor_[1];
            clearValues[0].color.float32[2] = clearColor_[2];
            clearValues[0].color.float32[3] = clearColor_[3];
            clearValues[1].depthStencil.depth = 1.0f;
            clearValues[1].depthStencil.stencil = 0;
            VkRenderPassBeginInfo renderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            renderPassBeginInfo.clearValueCount = (uint32_t) clearValues.size();
            renderPassBeginInfo.pClearValues = clearValues.data();

            swapchainContext->BindRenderTarget(imageIndex, &renderPassBeginInfo);

            vkCmdBeginRenderPass(m_cmdBuffer.buf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(m_cmdBuffer.buf, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchainContext->pipe.pipe);

            // Bind index and vertex buffers
            vkCmdBindIndexBuffer(m_cmdBuffer.buf, m_drawBuffer.idxBuf, 0, VK_INDEX_TYPE_UINT16);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(m_cmdBuffer.buf, 0, 1, &m_drawBuffer.vtxBuf, &offset);

            // Compute the view-projection transform.
            // Note all matrices (including OpenXR's) are column-major, right-handed.
            const auto &pose = layerView.pose;
            XrMatrix4x4f proj;
            XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_VULKAN, layerView.fov, 0.05f, 100.0f);
            XrMatrix4x4f toView;
            XrVector3f scale{1.f, 1.f, 1.f};
            XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scale);
            XrMatrix4x4f view;
            XrMatrix4x4f_InvertRigidBody(&view, &toView);
            XrMatrix4x4f vp;
            XrMatrix4x4f_Multiply(&vp, &proj, &view);

            auto pos = XrVector3f{quad.Pose.position.x, quad.Pose.position.y - 0.3f, quad.Pose.position.z};
            XrMatrix4x4f model;
            XrMatrix4x4f_CreateTranslationRotationScale(&model, &pos, &quad.Pose.orientation, &quad.Scale);
            XrMatrix4x4f mvp;
            XrMatrix4x4f_Multiply(&mvp, &vp, &model);
            vkCmdPushConstants(m_cmdBuffer.buf, m_pipelineLayout.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp.m), &mvp.m[0]);
            // Draw the plane
            vkCmdDrawIndexed(m_cmdBuffer.buf, m_drawBuffer.count.idx, 1, 0, 0, 0);

            // Render each cube
            for (const Cube &cube: cubes) {
                // Compute the model-view-projection transform and push it.
                XrMatrix4x4f model;
                XrMatrix4x4f_CreateTranslationRotationScale(&model, &cube.Pose.position, &cube.Pose.orientation, &cube.Scale);
                XrMatrix4x4f mvp;
                XrMatrix4x4f_Multiply(&mvp, &vp, &model);
                vkCmdPushConstants(m_cmdBuffer.buf, m_pipelineLayout.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp.m), &mvp.m[0]);

                // Draw the cube.
                vkCmdDrawIndexed(m_cmdBuffer.buf, m_drawBuffer.count.idx, 1, 0, 0, 0);
            }

            vkCmdEndRenderPass(m_cmdBuffer.buf);

            m_cmdBuffer.End();

            m_cmdBuffer.Exec(m_vkQueue);
            */
    }
}