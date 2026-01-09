#pragma once

#include <memory>
#include <vector>

#include "../Global.h"
#include "LveCamera.h"
#include "LveDevice.h"
#include "LveObject.h"
#include "LvePipeline.h"

namespace lve
{
class SliceMaskRenderSystem
{
public:
    SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass,
                          VkDescriptorSetLayout setLayouts);
    ~SliceMaskRenderSystem();

    SliceMaskRenderSystem(const SliceMaskRenderSystem&) = delete;
    SliceMaskRenderSystem& operator=(const SliceMaskRenderSystem&) = delete;

    void RenderBlank(const SliceInfo& sliceMaskInfo);
    void RenderGrindingWheelInstances(const SliceInstancedInfo& info);

    void BindBlankStencilPipeline(VkCommandBuffer commandBuffer);
    void BindBlankColorPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelStencilPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelColorPipeline(VkCommandBuffer commandBuffer);

private:
    void CreatePipelineLayout(const VkDescriptorSetLayout& setLayout);
    void CreatePipelines(VkRenderPass renderPass);
    void CreatePipeline(VkRenderPass renderPass);
    void CreateInstancedPipeline(VkRenderPass renderPass);

    void CreateBlankStencilPipeline(VkRenderPass renderPass);
    void CreateBlankColorPipeline(VkRenderPass renderPass);
    void CreateWheelStencilPipeline(VkRenderPass renderPass);
    void CreateWheelColorPipeline(VkRenderPass renderPass);

private:
    LveDevice& m_lveDevice;
    VkPipelineLayout m_pipelineLayout;

    // 棒料 (Blank) - 普通管线
    std::unique_ptr<LvePipeline> m_blankStencilPipeline;
    std::unique_ptr<LvePipeline> m_blankColorPipeline;  

    // 砂轮 (Wheel) - 实例管线
    std::unique_ptr<LvePipeline> m_grndWheelStencilPipeline;
    std::unique_ptr<LvePipeline> m_grndWheelColorPipeline;  
};

}  // namespace lve
