//
// Created by Stanislav Svědiroh on 15.06.2022.
//
#pragma once

#include "../pch.h"
#include "VulkanDevice.h"

namespace VulkanEngine {

    class VulkanDescriptorSetLayout {
    public:
        class Builder {
        public:
            explicit Builder(const VulkanDevice &device) : device_{device} {};

            Builder &AddBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags, uint32_t count = 1);

            [[nodiscard]] std::unique_ptr<VulkanDescriptorSetLayout> Build() const;

        private:
            const VulkanDevice &device_;
            std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings{};
        };

        VulkanDescriptorSetLayout(const VulkanDevice &device, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);

        ~VulkanDescriptorSetLayout();

        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout &) = delete;

        VulkanDescriptorSetLayout &operator=(const VulkanDescriptorSetLayout &) = delete;

        [[nodiscard]] VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout_; }

        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> GetBindings() { return bindings_; };

        VkDescriptorSetLayoutBinding &GetBindingAt(uint32_t index) { return bindings_[index]; };

    private:
        const VulkanDevice &device_;
        VkDescriptorSetLayout descriptorSetLayout_;
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings_;

        friend class LveDescriptorWriter;
    };

    class VulkanDescriptorPool {
    public:
        class Builder {
        public:
            explicit Builder(const VulkanDevice &device) : device_{device} {}

            Builder &AddPoolSize(VkDescriptorType descriptorType, uint32_t count);

            Builder &SetPoolFlags(VkDescriptorPoolCreateFlags flags);

            Builder &SetMaxSets(uint32_t count);

            [[nodiscard]] std::unique_ptr<VulkanDescriptorPool> Build() const;

        private:
            const VulkanDevice &device_;
            std::vector<VkDescriptorPoolSize> poolSizes_{};
            uint32_t maxSets_ = 1000;
            VkDescriptorPoolCreateFlags poolFlags_ = 0;
        };

        VulkanDescriptorPool(const VulkanDevice &device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags, const std::vector<VkDescriptorPoolSize> &poolSizes);

        ~VulkanDescriptorPool();

        VulkanDescriptorPool(const VulkanDescriptorPool &) = delete;

        VulkanDescriptorPool &operator=(const VulkanDescriptorPool &) = delete;

        bool AllocateDescriptor(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet &descriptor) const;

        void FreeDescriptors(std::vector<VkDescriptorSet> &descriptors) const;


        VkDescriptorPool GetPool() { return descriptorPool_; }

        void ResetPool();

        const VulkanDevice &GetDevice() { return device_; };

    private:
        const VulkanDevice &device_;
        VkDescriptorPool descriptorPool_{};

        friend class LveDescriptorWriter;
    };

    class VulkanDescriptorWriter {
    public:
        VulkanDescriptorWriter(VulkanDescriptorSetLayout &setLayout, VulkanDescriptorPool &pool);

        VulkanDescriptorWriter &WriteBuffer(uint32_t binding, VkDescriptorBufferInfo *bufferInfo);

        VulkanDescriptorWriter &WriteImage(uint32_t binding, VkDescriptorImageInfo *imageInfo);

        bool Build(VkDescriptorSet &set);

        void Overwrite(VkDescriptorSet &set);

    private:
        VulkanDescriptorSetLayout &setLayout_;
        VulkanDescriptorPool &pool_;
        std::vector<VkWriteDescriptorSet> writes_;
    };
}



