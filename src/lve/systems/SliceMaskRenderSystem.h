#pragma once

#include "LvePipeline.h"
#include "LveDevice.h"
#include "LveObject.h"
#include "LveCamera.h"
#include "../Global.h"

#include <memory>
#include <vector>

namespace lve {

	class SliceMaskRenderSystem {
	public:
		SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayouts);
		~SliceMaskRenderSystem();

		SliceMaskRenderSystem(const SliceMaskRenderSystem&) = delete;
		SliceMaskRenderSystem& operator=(const SliceMaskRenderSystem&) = delete;

		void RenderBlank(const SliceInfo& sliceMaskInfo);
		void RenderGrindingWheelInstances(const SliceInstancedInfo& info);

	private:
		void CreatePipelineLayout(const VkDescriptorSetLayout& setLayout);
		void CreatePipelines(VkRenderPass renderPass);
		void CreatePipeline(VkRenderPass renderPass);
		void CreateInstancedPipeline(VkRenderPass renderPass);

		LveDevice& m_lveDevice;

		std::unique_ptr<LvePipeline> m_lvePipeline;	// 主管线
		std::unique_ptr<LvePipeline> m_instancedPipeline;	// 砂轮实例管线
		VkPipelineLayout m_pipelineLayout;
		std::unique_ptr<LveModel> m_axisModel;
	};

}  // namespace lve
