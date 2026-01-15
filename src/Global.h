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

struct SliceInfo {
	VkCommandBuffer commandBuffer;
	lve::LveModel& model;
	const glm::mat4& modelMatrix;
	VkDescriptorSet globalDescriptorSet;
	float xM;
	float thickness;
};

struct SliceInstancedInfo {
	VkCommandBuffer commandBuffer;
	lve::LveModel& model;
	VkBuffer instanceBuffer;
	uint32_t instanceCount;
	VkDescriptorSet globalDescriptorSet;
	float xM;
	float thickness;
};

struct SliceFrameData {
	float xM;			// 截平面位置
	float thickness;	// 厚度，越小越好

	glm::vec2 worldXRange;
	glm::vec2 worldZRange;

	glm::mat4 blankModel;				// 棒料的世界变换矩阵
	std::vector<glm::mat4> wheelModels;	// 所有砂轮实例
};

struct SliceViewConfig {
	float xM;			// 截平面位置
	float thickness;	// 厚度，越小越好
	float xMin, xMax;	// 世界坐标X范围
	float zMin, zMax;	// 世界坐标Z范围
	uint32_t nX, nZ;	// 图像分辨率
};

enum class ContactMaskCode : uint32_t {
	EMPTY = 0u,				// 空
	BLANK_ONLY = 1u,		// 仅棒料
	GRNDWHEEL_ONLY = 2u,	// 仅砂轮
	INTERSECTION = 3u		// 棒料∩砂轮
};

enum class SliceDisplayMode : uint32_t {
	SHOW_ALL = 0,
	BLANK_ONLY = 1,
	GRNDWHEEL_ONLY = 2,
	INTERSECTION_ONLY = 3
};

struct ToolPath {
    size_t size{};
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> normals;
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