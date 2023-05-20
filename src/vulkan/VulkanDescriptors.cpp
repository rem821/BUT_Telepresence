//
// Created by Stanislav Svědiroh on 15.06.2022.
//
#include "VulkanDescriptors.h"

namespace VulkanEngine {


// *************** Descriptor Set Layout Builder *********************

    VulkanDescriptorSetLayout::Builder
    &VulkanDescriptorSetLayout::Builder::AddBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags, uint32_t count) {
        assert(bindings.count(binding) == 0 && "Binding already in use");
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = descriptorType;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;
        bindings[binding] = layoutBinding;
        return *this;
    }

    std::unique_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetLayout::Builder::Build() const {
        return std::make_unique<VulkanDescriptorSetLayout>(device_, bindings);
    }

// *************** Descriptor Set Layout *********************

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(const VulkanDevice &device, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
            : device_{device}, bindings_{bindings} {

        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (auto kv: bindings) {
            setLayoutBindings.push_back(kv.second);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
        descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

        CORE_ASSERT(vkCreateDescriptorSetLayout(device_.GetDevice(), &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout_) == VK_SUCCESS,
                    "failed to create descriptor set layout!")
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
        vkDestroyDescriptorSetLayout(device_.GetDevice(), descriptorSetLayout_, nullptr);
    }

// *************** Descriptor Pool Builder *********************

    VulkanDescriptorPool::Builder &VulkanDescriptorPool::Builder::AddPoolSize(
            VkDescriptorType descriptorType, uint32_t count) {
        poolSizes_.push_back({descriptorType, count});
        return *this;
    }

    VulkanDescriptorPool::Builder &VulkanDescriptorPool::Builder::SetPoolFlags(
            VkDescriptorPoolCreateFlags flags) {
        poolFlags_ = flags;
        return *this;
    }

    VulkanDescriptorPool::Builder &VulkanDescriptorPool::Builder::SetMaxSets(uint32_t count) {
        maxSets_ = count;
        return *this;
    }

    std::unique_ptr<VulkanDescriptorPool> VulkanDescriptorPool::Builder::Build() const {
        return std::make_unique<VulkanDescriptorPool>(device_, maxSets_, poolFlags_, poolSizes_);
    }

// *************** Descriptor Pool *********************

    VulkanDescriptorPool::VulkanDescriptorPool(const VulkanDevice &device, uint32_t maxSets, VkDescriptorPoolCreateFlags poolFlags,
                                               const std::vector<VkDescriptorPoolSize> &poolSizes) : device_{device} {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = maxSets;
        descriptorPoolInfo.flags = poolFlags;

        if (vkCreateDescriptorPool(device_.GetDevice(), &descriptorPoolInfo, nullptr, &descriptorPool_) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    VulkanDescriptorPool::~VulkanDescriptorPool() {
        vkDestroyDescriptorPool(device_.GetDevice(), descriptorPool_, nullptr);
    }

    bool VulkanDescriptorPool::AllocateDescriptor(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet &descriptor) const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;

        // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
        // a new pool whenever an old pool fills up. But this is beyond our current scope
        return vkAllocateDescriptorSets(device_.GetDevice(), &allocInfo, &descriptor) == VK_SUCCESS;
    }

    void VulkanDescriptorPool::FreeDescriptors(std::vector<VkDescriptorSet> &descriptors) const {
        vkFreeDescriptorSets(
                device_.GetDevice(),
                descriptorPool_,
                static_cast<uint32_t>(descriptors.size()),
                descriptors.data());
    }

    void VulkanDescriptorPool::ResetPool() {
        vkResetDescriptorPool(device_.GetDevice(), descriptorPool_, 0);
    }

// *************** Descriptor Writer *********************

    VulkanDescriptorWriter::VulkanDescriptorWriter(VulkanDescriptorSetLayout &setLayout, VulkanDescriptorPool &pool)
            : setLayout_{setLayout}, pool_{pool} {}

    VulkanDescriptorWriter &VulkanDescriptorWriter::WriteBuffer(uint32_t binding, VkDescriptorBufferInfo *bufferInfo) {
        CORE_ASSERT(setLayout_.GetBindings().count(binding) == 1, "Layout does not contain specified binding")

        auto &bindingDescription = setLayout_.GetBindingAt(binding);

        CORE_ASSERT(bindingDescription.descriptorCount == 1, "Binding single descriptor info, but binding expects multiple")

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pBufferInfo = bufferInfo;
        write.descriptorCount = 1;

        writes_.push_back(write);
        return *this;
    }

    VulkanDescriptorWriter &VulkanDescriptorWriter::WriteImage(uint32_t binding, VkDescriptorImageInfo *imageInfo) {
        CORE_ASSERT(setLayout_.GetBindings().count(binding) == 1, "Layout does not contain specified binding")

        auto &bindingDescription = setLayout_.GetBindingAt(binding);

        CORE_ASSERT(bindingDescription.descriptorCount == 1, "Binding single descriptor info, but binding expects multiple")

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pImageInfo = imageInfo;
        write.descriptorCount = 1;

        writes_.push_back(write);
        return *this;
    }

    bool VulkanDescriptorWriter::Build(VkDescriptorSet &set) {
        bool success = pool_.AllocateDescriptor(setLayout_.GetDescriptorSetLayout(), set);
        if (!success) {
            return false;
        }
        Overwrite(set);
        return true;
    }

    void VulkanDescriptorWriter::Overwrite(VkDescriptorSet &set) {
        for (auto &write: writes_) {
            write.dstSet = set;
        }
        vkUpdateDescriptorSets(pool_.GetDevice().GetDevice(), writes_.size(), writes_.data(), 0, nullptr);
    }

}
