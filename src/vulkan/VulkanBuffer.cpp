//
// Created by Stanislav Svědiroh on 15.06.2022.
//
/*
 * Encapsulates a vulkan buffer
 *
 * Initially based off VulkanBuffer by Sascha Willems -
 * https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanBuffer.h
 */

#include "VulkanBuffer.h"

namespace VulkanEngine {

/**
 * Returns the minimum instance size required to be compatible with devices minOffsetAlignment
 *
 * @param instanceSize The size of an instance
 * @param minOffsetAlignment The minimum required alignment, in bytes, for the offset member (eg
 * minUniformBufferOffsetAlignment)
 *
 * @return VkResult of the buffer mapping call
 */
    VkDeviceSize VulkanBuffer::GetAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
        if (minOffsetAlignment > 0) {
            return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
        }
        return instanceSize;
    }

    VulkanBuffer::VulkanBuffer(
            VulkanDevice &device,
            VkDeviceSize instanceSize,
            uint32_t instanceCount,
            VkBufferUsageFlags usageFlags,
            VkMemoryPropertyFlags memoryPropertyFlags,
            VkDeviceSize minOffsetAlignment)
            : engineDevice_{device},
              instanceCount_{instanceCount},
              instanceSize_{instanceSize},
              usageFlags_{usageFlags},
              memoryPropertyFlags_{memoryPropertyFlags} {
        alignmentSize_ = GetAlignment(instanceSize, minOffsetAlignment);
        bufferSize_ = alignmentSize_ * instanceCount;
        device.CreateBuffer(bufferSize_, usageFlags, memoryPropertyFlags, buffer_, memory_);
    }

    VulkanBuffer::~VulkanBuffer() {
        Unmap();
        vkDestroyBuffer(engineDevice_.GetDevice(), buffer_, nullptr);
        vkFreeMemory(engineDevice_.GetDevice(), memory_, nullptr);
    }

/**
 * ChunkDeserializer a memory range of this buffer. If successful, mapped points to the specified buffer range.
 *
 * @param size (Optional) Size of the memory range to map. Pass VK_WHOLE_SIZE to map the complete
 * buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the buffer mapping call
 */
    VkResult VulkanBuffer::Map(VkDeviceSize size, VkDeviceSize offset) {
        CORE_ASSERT(buffer_ && memory_, "Called map on buffer before create");
        return vkMapMemory(engineDevice_.GetDevice(), memory_, offset, size, 0, &mapped_);
    }

/**
 * Unmap a mapped memory range
 *
 * @note Does not return a result as vkUnmapMemory can't fail
 */
    void VulkanBuffer::Unmap() {
        if (mapped_) {
            vkUnmapMemory(engineDevice_.GetDevice(), memory_);
            mapped_ = nullptr;
        }
    }

/**
 * Copies the specified data to the mapped buffer. Default value writes whole buffer range
 *
 * @param data Pointer to the data to copy
 * @param size (Optional) Size of the data to copy. Pass VK_WHOLE_SIZE to flush the complete buffer
 * range.
 * @param offset (Optional) Byte offset from beginning of mapped region
 *
 */
    void VulkanBuffer::WriteToBuffer(void *data, VkDeviceSize size, VkDeviceSize offset) {
        CORE_ASSERT(mapped_, "Cannot copy to unmapped buffer");

        if (size == VK_WHOLE_SIZE) {
            memcpy(mapped_, data, bufferSize_);
        } else {
            char *memOffset = (char *) mapped_;
            memOffset += offset;
            memcpy(memOffset, data, size);
        }
    }

/**
 * Flush a memory range of the buffer to make it visible to the device
 *
 * @note Only required for non-coherent memory
 *
 * @param size (Optional) Size of the memory range to flush. Pass VK_WHOLE_SIZE to flush the
 * complete buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the flush call
 */
    VkResult VulkanBuffer::Flush(VkDeviceSize size, VkDeviceSize offset) {
        VkMappedMemoryRange mappedRange = {};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory_;
        mappedRange.offset = offset;
        mappedRange.size = size;
        return vkFlushMappedMemoryRanges(engineDevice_.GetDevice(), 1, &mappedRange);
    }

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 *
 * @param size (Optional) Size of the memory range to invalidate. Pass VK_WHOLE_SIZE to invalidate
 * the complete buffer range.
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkResult of the invalidate call
 */
    VkResult VulkanBuffer::Invalidate(VkDeviceSize size, VkDeviceSize offset) {
        VkMappedMemoryRange mappedRange = {};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = memory_;
        mappedRange.offset = offset;
        mappedRange.size = size;
        return vkInvalidateMappedMemoryRanges(engineDevice_.GetDevice(), 1, &mappedRange);
    }

/**
 * Create a buffer info descriptor
 *
 * @param size (Optional) Size of the memory range of the descriptor
 * @param offset (Optional) Byte offset from beginning
 *
 * @return VkDescriptorBufferInfo of specified offset and range
 */
    VkDescriptorBufferInfo VulkanBuffer::DescriptorInfo(VkDeviceSize size, VkDeviceSize offset) {
        return VkDescriptorBufferInfo{
                buffer_,
                offset,
                size,
        };
    }

/**
 * Copies "instanceSize" bytes of data to the mapped buffer at an offset of index * alignmentSize
 *
 * @param data Pointer to the data to copy
 * @param index Used in offset calculation
 *
 */
    void VulkanBuffer::WriteToIndex(void *data, int index) {
        WriteToBuffer(data, instanceSize_, index * alignmentSize_);
    }

/**
 *  Flush the memory range at index * alignmentSize of the buffer to make it visible to the device
 *
 * @param index Used in offset calculation
 *
 */
    VkResult VulkanBuffer::FlushIndex(int index) { return Flush(alignmentSize_, index * alignmentSize_); }

/**
 * Create a buffer info descriptor
 *
 * @param index Specifies the region given by index * alignmentSize
 *
 * @return VkDescriptorBufferInfo for instance at index
 */
    VkDescriptorBufferInfo VulkanBuffer::DescriptorInfoForIndex(int index) {
        return DescriptorInfo(alignmentSize_, index * alignmentSize_);
    }

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 *
 * @param index Specifies the region to invalidate: index * alignmentSize
 *
 * @return VkResult of the invalidate call
 */
    VkResult VulkanBuffer::InvalidateIndex(int index) {
        return Invalidate(alignmentSize_, index * alignmentSize_);
    }
}