#pragma once

#include "LveCamera.h"
#include "LveObject.h"

#include <vulkan/vulkan.h>

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

	struct alignas(16) MaterialUBO {
		glm::vec4 baseColorFactor;
		glm::vec4 uvTilingOffset;
		glm::vec4 pbrAoAlpha;
		glm::uvec4 flags;
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
	};

	struct RenderShared {
		LveDevice& lveDevice;
		LveModel& lveModel;
		std::unordered_map<uint32_t, std::vector<VkDescriptorSet>>& submeshTextureSets;
		std::unordered_map<uint32_t, std::vector<
			std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>>> submeshMatSets;
	};

}