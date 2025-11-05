#pragma once

#include "LvePipeline.h"
#include "LveDevice.h"
#include "LveObject.h"
#include "LveCamera.h"
#include "LveFrameInfo.h"

#include <memory>
#include <vector>

namespace lve {

class PointLightSystem {
public:
	PointLightSystem(LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
	~PointLightSystem();

	PointLightSystem(const PointLightSystem&) = delete;
	PointLightSystem& operator=(const PointLightSystem&) = delete;

	void Render(FrameInfo& frameInfo); //不将camera作为成员变量，能在多个渲染系统之间共享相机对象

private:
	void CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout);
	void CreatePipelines(VkRenderPass renderPass);

	LveDevice& m_lveDevice;

	std::unique_ptr<LvePipeline> m_lvePipeline;	// 主三角形管线
	VkPipelineLayout m_pipelineLayout;
	std::unique_ptr<LveModel> m_axisModel;
};

}  // namespace lve
