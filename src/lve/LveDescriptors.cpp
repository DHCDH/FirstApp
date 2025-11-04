#include "LveDescriptors.h"

// std
#include <cassert>
#include <stdexcept>

namespace lve {

    // *************** Descriptor Set Layout Builder *********************

    /*
     * @param binding
     * @param descriptorType 期望的描述符类型，如统一缓冲区、图像缓冲区、存储缓冲区等
     * @param stageFlags 着色器阶段标志，告诉Vulkan哪些着色器阶段可以访问这个绑定
     * @param count 每个绑定可以包含该类型描述符数组，在着色器中通过索引访问
     */
    LveDescriptorSetLayout::Builder& LveDescriptorSetLayout::Builder::AddBinding(
        uint32_t binding,
        VkDescriptorType descriptorType,
        VkShaderStageFlags stageFlags,
        uint32_t count) 
    {
        assert(m_bindings.count(binding) == 0 && "Binding already in use"); // 确保在指定索引处没有添加绑定
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = descriptorType;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;
        m_bindings[binding] = layoutBinding;
        return *this;
    }

    std::unique_ptr<LveDescriptorSetLayout> LveDescriptorSetLayout::Builder::Build() const 
    {
        return std::make_unique<LveDescriptorSetLayout>(m_lveDevice, m_bindings);
    }

    // *************** Descriptor Set Layout *********************

    LveDescriptorSetLayout::LveDescriptorSetLayout(
        LveDevice& lveDevice, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
        : m_lveDevice{ lveDevice }, m_bindings{ bindings }
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (const auto& kv : bindings) {
            setLayoutBindings.push_back(kv.second);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
        descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

        if (vkCreateDescriptorSetLayout(
            lveDevice.device(),
            &descriptorSetLayoutInfo,
            nullptr,
            &m_descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    LveDescriptorSetLayout::~LveDescriptorSetLayout() 
    {
        vkDestroyDescriptorSetLayout(m_lveDevice.device(), m_descriptorSetLayout, nullptr);
    }

    // *************** Descriptor Pool Builder *********************

    LveDescriptorPool::Builder& LveDescriptorPool::Builder::AddPoolSize(
        VkDescriptorType descriptorType, uint32_t count) 
    {
        m_poolSizes.push_back({ descriptorType, count });
        return *this;
    }

    LveDescriptorPool::Builder& LveDescriptorPool::Builder::SetPoolFlags(
        VkDescriptorPoolCreateFlags flags) {
        m_poolFlags = flags;
        return *this;
    }

    LveDescriptorPool::Builder& LveDescriptorPool::Builder::SetMaxSets(uint32_t count) 
    {
        m_maxSets = count;
        return *this;
    }

    std::unique_ptr<LveDescriptorPool> LveDescriptorPool::Builder::Build() const 
    {
        return std::make_unique<LveDescriptorPool>(m_lveDevice, m_maxSets, m_poolFlags, m_poolSizes);
    }

    // *************** Descriptor Pool *********************

    LveDescriptorPool::LveDescriptorPool(
        LveDevice& lveDevice,
        uint32_t maxSets,
        VkDescriptorPoolCreateFlags poolFlags,
        const std::vector<VkDescriptorPoolSize>& poolSizes)
        : m_lveDevice{ lveDevice }
    {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = maxSets;
        descriptorPoolInfo.flags = poolFlags;

        if (vkCreateDescriptorPool(lveDevice.device(), &descriptorPoolInfo, nullptr, &m_descriptorPool) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    LveDescriptorPool::~LveDescriptorPool() 
    {
        vkDestroyDescriptorPool(m_lveDevice.device(), m_descriptorPool, nullptr);
    }

    bool LveDescriptorPool::AllocateDescriptor(
        const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor) const 
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;

        // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
        // a new pool whenever an old pool fills up. But this is beyond our current scope
        if (vkAllocateDescriptorSets(m_lveDevice.device(), &allocInfo, &descriptor) != VK_SUCCESS) {
            return false;
        }
        return true;
    }

    void LveDescriptorPool::FreeDescriptors(std::vector<VkDescriptorSet>& descriptors) const 
    {
        vkFreeDescriptorSets(
            m_lveDevice.device(),
            m_descriptorPool,
            static_cast<uint32_t>(descriptors.size()),
            descriptors.data());
    }

    void LveDescriptorPool::ResetPool() 
    {
        vkResetDescriptorPool(m_lveDevice.device(), m_descriptorPool, 0);
    }

    // *************** Descriptor Writer *********************

    LveDescriptorWriter::LveDescriptorWriter(LveDescriptorSetLayout& setLayout, LveDescriptorPool& pool)
        : m_setLayout{ setLayout }, m_pool{ pool } {
    }

    LveDescriptorWriter& LveDescriptorWriter::WriteBuffer(
        uint32_t binding, VkDescriptorBufferInfo* bufferInfo) 
    {
        assert(m_setLayout.m_bindings.count(binding) == 1 && "Layout does not contain specified binding");

        auto& bindingDescription = m_setLayout.m_bindings[binding];

        assert(
            bindingDescription.descriptorCount == 1 &&
            "Binding single descriptor info, but binding expects multiple");

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pBufferInfo = bufferInfo;
        write.descriptorCount = 1;

        m_writes.push_back(write);
        return *this;
    }

    LveDescriptorWriter& LveDescriptorWriter::WriteImage(
        uint32_t binding, VkDescriptorImageInfo* imageInfo) 
    {
        assert(m_setLayout.m_bindings.count(binding) == 1 && "Layout does not contain specified binding");

        auto& bindingDescription = m_setLayout.m_bindings[binding];

        assert(
            bindingDescription.descriptorCount == 1 &&
            "Binding single descriptor info, but binding expects multiple");

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pImageInfo = imageInfo;
        write.descriptorCount = 1;

        m_writes.push_back(write);
        return *this;
    }

    bool LveDescriptorWriter::Build(VkDescriptorSet& set) 
    {
        bool success = m_pool.AllocateDescriptor(m_setLayout.GetDescriptorSetLayout(), set);
        if (!success) {
            return false;
        }
        Overwrite(set);
        return true;
    }

    void LveDescriptorWriter::Overwrite(VkDescriptorSet& set) 
    {
        for (auto& write : m_writes) {
            write.dstSet = set;
        }
        vkUpdateDescriptorSets(m_pool.m_lveDevice.device(), m_writes.size(), m_writes.data(), 0, nullptr);
    }

}  // namespace lve