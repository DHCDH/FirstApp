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
	void RenderInstances(FrameInfo& frameInfo, const bool& shown);

private:
	void CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& globalSetLayout);
	void CreatePipelines(VkRenderPass renderPass);
	void CreatePipeline(VkRenderPass renderPass);
	void CreateInstancedPipeline(VkRenderPass renderPass);
	void CreateInvisibleInstancedPipeline(VkRenderPass renderPass);
	void CreateTranslucentPipeline(VkRenderPass renderPass);
	void CreateOutlinePipeline(VkRenderPass renderPass);

	LveDevice& m_lveDevice;

	std::unique_ptr<LvePipeline> m_lvePipeline;	// 主三角形管线
	std::unique_ptr<LvePipeline> m_lvePipelineInstanced;	// 实例化管线
	std::unique_ptr<LvePipeline> m_lvePipelineInstancedInvisible;	// 不可见实例化管线
	std::unique_ptr<LvePipeline> m_lvePipelineTranslucent;	// 半透明管线
    std::unique_ptr<LvePipeline> m_lvePipelineOutline;	// 线框管线
	VkPipelineLayout m_pipelineLayout;
	std::unique_ptr<LveModel> m_axisModel;
};

}  // namespace lve
