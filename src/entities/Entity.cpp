#include "Entity.h"

#include <array>
#include <memory>

void Entity::CreateMaterialParamSetsForSubmesh(uint32_t objId, uint32_t submeshIndex, const lve::MaterialUBO& u)
{
    auto& slot = m_renderContext.submeshMatSets[objId][submeshIndex];
    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        auto buf = std::make_unique<lve::LveBuffer>(
            m_renderContext.device, sizeof(lve::MaterialUBO), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buf->Map();
        buf->WriteToBuffer((void*)&u);
        VkDescriptorBufferInfo bi = buf->DescriptorInfo();
        lve::LveDescriptorWriter(m_renderContext.materialParamSetLayout, m_renderContext.materialParamPool)
            .WriteBuffer(0, &bi)
            .Build(slot[i]);
        m_renderContext.materialParamBuffers.push_back(std::move(buf));
    }
}

void Entity::AssignMaterialParamsToObject(uint32_t objId, const lve::MaterialUBO& init)
{
    lve::MaterialGPU mgpu;
    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        mgpu.ubos[i] = std::make_unique<lve::LveBuffer>(
            m_renderContext.device,
            sizeof(lve::MaterialUBO), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        mgpu.ubos[i]->Map();
        mgpu.ubos[i]->WriteToBuffer((void*)&init);

        VkDescriptorBufferInfo bufferInfo = mgpu.ubos[i]->DescriptorInfo();
        lve::LveDescriptorWriter(m_renderContext.materialParamSetLayout, m_renderContext.materialParamPool)
            .WriteBuffer(0, &bufferInfo)                      // set=2, binding=0
            .Build(mgpu.sets[i]);
    }
    m_renderContext.objectMaterialParams[objId] = std::move(mgpu);
}