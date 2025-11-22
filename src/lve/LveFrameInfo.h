#pragma once

#include "LveCamera.h"
#include "LveObject.h"
#include "lveSwapChain.h"

#include <vulkan/vulkan.h>
#include <array>

namespace lve {

#define MAX_LIGHTS 20

struct PointLight {
	glm::vec4 position{};	// ignore w
	glm::vec4 color{};
};

struct GlobalUbo {
	glm::mat4 projection{ 1.f };
	glm::mat4 view{ 1.f };
	glm::mat4 inverseView{ 1.f };	// 通过视图逆矩阵最后一列获取相机位置

	/*点光源*/
	glm::vec4 ambientLightColor{ 1.f, 1.f, 1.f, .5f }; // w is intensity
	PointLight pointLights[MAX_LIGHTS];
	int numLights;
};

struct MaterialGPU {
	std::array<std::unique_ptr<LveBuffer>, LveSwapChain::MAX_FRAMES_IN_FLIGHT> ubos;
	std::array<VkDescriptorSet, LveSwapChain::MAX_FRAMES_IN_FLIGHT> sets;

	MaterialGPU() = default;
};

struct alignas(16) MaterialUBO {
	glm::vec4 baseColorFactor;
	glm::vec4 uvTilingOffset;
	glm::vec4 pbrAoAlpha;
	glm::uvec4 flags;
};

struct InstanceData {
	glm::mat4 modelMatrix;
};

struct InstanceBatch {
	LveModel* model = nullptr;
	VkBuffer instanceBuffer = VK_NULL_HANDLE;	// 实例数据所在缓冲
	VkDeviceSize instanceStride = 0;			// 每个实例的字节大小
	uint32_t instanceCount = 0;					// 实例数量

	VkDescriptorSet set1 = VK_NULL_HANDLE;
	VkDescriptorSet set2 = VK_NULL_HANDLE;
};

struct FrameInfo {
	int frameIndex;
	float frameTime;
	VkCommandBuffer commandBuffer;
	LveCamera& camera;
	VkDescriptorSet globalDescriptorSet;
	LveObject::Map& objects;

	const std::unordered_map<uint32_t, VkDescriptorSet>* materialDescriptorSets = nullptr;	// set = 1，每个对象的材质纹理描述符集
	const std::unordered_map<uint32_t, VkDescriptorSet>* materialParamSets = nullptr;	// set = 2，每个对象的材质参数UBO描述符集
	const std::unordered_map<uint32_t, std::vector<VkDescriptorSet>>* submeshTexSets = nullptr;          // set=1
	const std::unordered_map<uint32_t, std::vector<VkDescriptorSet>>* submeshMatSetThisFrame = nullptr;  // set=2（本帧）
	VkDescriptorSet dummyTexSet = VK_NULL_HANDLE;  // 占位 set=1
	VkDescriptorSet dummyMatSet = VK_NULL_HANDLE;	// 占位 set=2（本帧）

	std::vector<InstanceBatch> instanceBatches;
};

}