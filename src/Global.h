#pragma once

#include "lve/LveFrameInfo.h"
#include "lve/lveSwapChain.h"
#include "lve/LveTextureManager.h"
#include <iostream>

struct RenderContext {
	lve::LveDevice& device;
	std::unordered_map<uint32_t, std::vector<
		std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>>>& submeshMatSets;
	std::unordered_map<uint32_t, std::vector<VkDescriptorSet>>& submeshTextureSets;
	std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>& dummyMatSets;
	lve::LveTextureManager& textureManager;
	lve::LveDescriptorSetLayout& materialParamSetLayout;
	lve::LveDescriptorPool& materialParamPool;
	std::vector<std::unique_ptr<lve::LveBuffer>>& materialParamBuffers;
	std::unordered_map<uint32_t, lve::MaterialGPU>& objectMaterialParams;
};

inline void PrintMat4(const glm::mat4& M, const std::string& name = "")
{
	std::cout << name << ": " << "\n";
	std::cout << M[0].x << " " << M[0].y << " " << M[0].z << " " << M[0].w << "\n";
	std::cout << M[1].x << " " << M[1].y << " " << M[1].z << " " << M[1].w << "\n";
	std::cout << M[2].x << " " << M[2].y << " " << M[2].z << " " << M[2].w << "\n";
	std::cout << M[3].x << " " << M[3].y << " " << M[3].z << " " << M[3].w << "\n";
}

inline void PrintVec3(const glm::vec3& v, const std::string& name = "")
{
	std::cout << name << "(";
    std::cout << v.x << " " << v.y << " " << v.z << ")\n";
}