//
// Created by Stanislav Svědiroh on 15.06.2022.
//
#pragma once

#include "../pch.h"
#include "../xr_linear.h"

namespace VulkanEngine {

    struct GlobalUbo {
        XrMatrix4x4f projection{1.f};
        XrMatrix4x4f view{1.f};
        XrMatrix4x4f inverseView{1.f};
    };

    struct FrameInfo {
        VkCommandBuffer commandBuffer;
        VkDescriptorSet globalDescriptorSet;
    };
}