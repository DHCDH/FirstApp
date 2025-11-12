#pragma once

#include <memory>
#include <unordered_map>
#include <string>

#include "LveDevice.h"
#include "LveTexture.h"
#include "LveDescriptors.h"

namespace lve {

class LveTextureManager 
{
public:
	LveTextureManager(
		LveDevice& lveDevice, 
		VkSampler sharedSmapler, 
		LveDescriptorSetLayout& materialSetLayout, 
		LveDescriptorPool& materialPool);

	LveTexture* LoadOrGet(const std::string& path, bool srgb = true);	// 加载或服用纹理对象
	VkDescriptorSet GetOrCreateMaterialSet(const std::string& path, bool srgb = true);	// 获取或创建材质描述符集（set = 1, binding = 0)

	bool HasTexture(const std::string& path) const { return m_textures.count(path) != 0; }
	LveTexture* GetTexture(const std::string& path) const {
		auto it = m_textures.find(path); return it == m_textures.end() ? nullptr : it->second.get();
	}

private:
	LveDevice& m_device;
	VkSampler m_sharedSampler = VK_NULL_HANDLE; // 若传入共享采样器，新纹理将共用它
	LveDescriptorSetLayout& m_materialSetLayout; // set=1 布局
	LveDescriptorPool& m_materialPool;      // set=1 池

	// 资源缓存
	std::unordered_map<std::string, std::unique_ptr<LveTexture>> m_textures;
	std::unordered_map<std::string, VkDescriptorSet> m_materialSets;

};

}