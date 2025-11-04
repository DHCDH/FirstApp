#pragma once

#include "LveCamera.h"

#include <vulkan/vulkan.h>

namespace lve {

struct FrameInfo {
	int frameIndex;
	VkCommandBuffer commandBuffer;
	LveCamera& camera;
	VkDescriptorSet globalDescriptorSet;
};

}