#pragma once

#include "LvePipeline.h"
#include "LveDevice.h"
#include "LveObject.h"
#include "LveCamera.h"
#include "LveFrameInfo.h"

#include <memory>
#include <vector>

namespace lve {

class RenderSystem {
public:
	RenderSystem(LveDevice& device, VkRenderPass renderPass, const std::vector<VkDescriptorSetLayout>& setLayouts);
	~RenderSystem();

	RenderSystem(const RenderSystem&) = delete;
	RenderSystem& operator=(const RenderSystem&) = delete;

	void RenderObjects(FrameInfo& frameInfo);
	void RenderInstances(FrameInfo& frameInfo);

private:
	void CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& globalSetLayout);
	void CreatePipelines(VkRenderPass renderPass);
	void CreatePipeline(VkRenderPass renderPass);
	void CreateInstancedPipeline(VkRenderPass renderPass);

	LveDevice& m_lveDevice;

	std::unique_ptr<LvePipeline> m_lvePipeline;	// 主三角形管线
	std::unique_ptr<LvePipeline> m_lvePipelineInstanced;	// 实例化管线
	VkPipelineLayout m_pipelineLayout;
	std::unique_ptr<LveModel> m_axisModel;
};

}  // namespace lve
