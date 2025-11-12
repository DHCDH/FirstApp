#include "LveTextureManager.h"

namespace lve {
    LveTextureManager::LveTextureManager(
        LveDevice& device,
        VkSampler sharedSampler,
        LveDescriptorSetLayout& materialSetLayout,
        LveDescriptorPool& materialPool)
        : m_device(device),
        m_sharedSampler(sharedSampler),
        m_materialSetLayout(materialSetLayout),
        m_materialPool(materialPool) 
    {
    }

    LveTexture* LveTextureManager::LoadOrGet(const std::string& path, bool srgb) 
    {
        if (auto it = m_textures.find(path); it != m_textures.end()) {
            return it->second.get();
        }

        auto tex = std::make_unique<LveTexture>(m_device, path, m_sharedSampler, srgb);

        LveTexture* ptr = tex.get();
        m_textures.emplace(path, std::move(tex));
        return ptr;
    }

    VkDescriptorSet LveTextureManager::GetOrCreateMaterialSet(const std::string& path, bool srgb) 
    {
        if (auto it = m_materialSets.find(path); it != m_materialSets.end()) {
            return it->second;
        }

        LveTexture* tex = LoadOrGet(path, srgb);
        VkDescriptorImageInfo imgInfo = tex->DescriptorInfo();

        VkDescriptorSet set{};
        LveDescriptorWriter(m_materialSetLayout, m_materialPool)
            .WriteImage(/*binding=*/0, &imgInfo)  // set=1, binding=0
            .Build(set);

        m_materialSets.emplace(path, set);
        return set;
    }
}