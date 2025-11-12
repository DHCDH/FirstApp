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

	void RenderObjects(FrameInfo& frameInfo); //不将camera作为成员变量，能在多个渲染系统之间共享相机对象

private:
	void CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& globalSetLayout);
	void CreatePipelines(VkRenderPass renderPass);

	LveDevice& m_lveDevice;

	std::unique_ptr<LvePipeline> m_lvePipeline;	// 主三角形管线
	std::unique_ptr<LvePipeline> m_axisPipeline;	// 坐标轴线框管线
	VkPipelineLayout m_pipelineLayout;
	std::unique_ptr<LveModel> m_axisModel;
};

}  // namespace lve
