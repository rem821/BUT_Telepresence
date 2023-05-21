//
// Created by Stanislav Svědiroh on 15.06.2022.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"

namespace VulkanEngine {

    class VulkanBuffer {
    public:
        VulkanBuffer(VulkanDevice &device, VkDeviceSize instanceSize, uint32_t instanceCount, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags,
                     VkDeviceSize minOffsetAlignment = 1);

        ~VulkanBuffer();

        VulkanBuffer(const VulkanBuffer &) = delete;

        VulkanBuffer &operator=(const VulkanBuffer &) = delete;

        VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        void Unmap();

        void WriteToBuffer(void *data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        VkResult Flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        VkDescriptorBufferInfo DescriptorInfo(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        VkResult Invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        void WriteToIndex(void *data, int index);

        VkResult FlushIndex(int index);

        VkDescriptorBufferInfo DescriptorInfoForIndex(int index);

        VkResult InvalidateIndex(int index);

        [[nodiscard]] VkBuffer GetBuffer() const { return buffer_; }

        [[nodiscard]] void *GetMappedMemory() const { return mapped_; }

        [[nodiscard]] uint32_t GetInstanceCount() const { return instanceCount_; }

        [[nodiscard]] VkDeviceSize GetInstanceSize() const { return instanceSize_; }

        [[nodiscard]] VkDeviceSize GetAlignmentSize() const { return instanceSize_; }

        [[nodiscard]] VkBufferUsageFlags GetUsageFlags() const { return usageFlags_; }

        [[nodiscard]] VkMemoryPropertyFlags GetMemoryPropertyFlags() const { return memoryPropertyFlags_; }

        [[nodiscard]] VkDeviceSize GetBufferSize() const { return bufferSize_; }

    private:
        static VkDeviceSize GetAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

        VulkanDevice &engineDevice_;
        void *mapped_ = nullptr;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VkDeviceMemory memory_ = VK_NULL_HANDLE;

        VkDeviceSize bufferSize_;
        uint32_t instanceCount_;
        VkDeviceSize instanceSize_;
        VkDeviceSize alignmentSize_;
        VkBufferUsageFlags usageFlags_;
        VkMemoryPropertyFlags memoryPropertyFlags_;
    };


}