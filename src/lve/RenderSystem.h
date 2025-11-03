#pragma once

#include "LvePipeline.h"
#include "LveDevice.h"
#include "LveObject.h"
#include "LveCamera.h"

#include <memory>
#include <vector>

namespace lve {

class RenderSystem {
public:
	RenderSystem(LveDevice& device, VkRenderPass renderPass);
	~RenderSystem();

	RenderSystem(const RenderSystem&) = delete;
	RenderSystem& operator=(const RenderSystem&) = delete;

	void RenderGameObjects(VkCommandBuffer commandBuffer, 
						   std::vector<LveObject>& gameObjects, 
						   const LveCamera& camera); //不将camera作为成员变量，能在多个渲染系统之间共享相机对象
	void RenderAxis(VkCommandBuffer commandBuffer, const LveCamera& camera, VkExtent2D extent);

private:
	void CreatePipelineLayout();
	void CreatePipeline(VkRenderPass renderPass);
	void CreatePipelines(VkRenderPass renderPass);
	void CreateAxisVertices();

	LveDevice& m_lveDevice;

	std::unique_ptr<LvePipeline> m_lvePipeline;	// 主三角形管线
	std::unique_ptr<LvePipeline> m_axisPipeline;	// 坐标轴线框管线
	VkPipelineLayout m_pipelineLayout;
	std::unique_ptr<LveModel> m_axisModel;
};

}  // namespace lve
