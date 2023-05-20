//
// Created by standa on 10.5.23.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"
#include <shaderc/shaderc.hpp>
#include <shaderc/status.h>

namespace VulkanEngine {

    struct PipelineConfigInfo {
        PipelineConfigInfo() = default;

        PipelineConfigInfo(const PipelineConfigInfo &) = delete;

        PipelineConfigInfo operator=(const PipelineConfigInfo &) = delete;

        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
        VkPipelineViewportStateCreateInfo viewportInfo{};
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        std::vector<VkDynamicState> dynamicStateEnables;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
        VkPipelineLayout pipelineLayout{};
        VkRenderPass renderPass{};
        uint32_t subpass = 0;
    };

    class VulkanPipeline {
    public:
        VulkanPipeline(VulkanDevice &engineDevice, const PipelineConfigInfo &configInfo);

        ~VulkanPipeline();

        VulkanPipeline(const VulkanPipeline &) = delete;

        VulkanPipeline operator=(const VulkanPipeline &) = delete;

        void Bind(VkCommandBuffer commandBuffer);

        static void DefaultPipelineConfig(PipelineConfigInfo &configInfo, std::vector<VkVertexInputBindingDescription> bindingDesc,
                                          std::vector<VkVertexInputAttributeDescription> &attrDesc);

        static void EnableAlphaBlending(PipelineConfigInfo &configInfo);

    private:
        void CreateGraphicsPipeline(const PipelineConfigInfo &configInfo);

        void CreateShaderModule(const std::vector<uint32_t> &code, VkShaderModule *shaderModule);

        // Compile a shader to a SPIR_V binary
        std::vector<uint32_t> CompileGLSLShader(const std::string &name, shaderc_shader_kind kind, const std::string &source);

        VulkanDevice &engineDevice_;
        VkPipeline graphicsPipeline_;
        VkShaderModule vertShaderModule_;
        VkShaderModule fragShaderModule_;
    };
}