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
    void RenderPlaneInjection(VkCommandBuffer commandBuffer,
                              VkDescriptorSet globalDescriptorSet, float yM);

    void BindBlankStencilPipeline(VkCommandBuffer commandBuffer);
    void BindBlankColorPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelStencilFrontPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelStencilBackPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelWireframePipeline(VkCommandBuffer commandBuffer);
    void BindBlankDepthPipeline(VkCommandBuffer commandBuffer);
    
    void BindPlaneInjectionPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelEdgePipeline(VkCommandBuffer commandBuffer);

private:
    void CreatePipelineLayout(const VkDescriptorSetLayout& setLayout);
    void CreatePipelines(VkRenderPass renderPass);
    void CreatePipeline(VkRenderPass renderPass);
    void CreateInstancedPipeline(VkRenderPass renderPass);

    void CreateBlankStencilPipeline(VkRenderPass renderPass);
    void CreateBlankColorPipeline(VkRenderPass renderPass);
    void CreateGrindingWheelStencilFrontPipeline(VkRenderPass renderPass);
    void CreateGrindingWheelStencilBackPipeline(VkRenderPass renderPass);
    void CreateGrindingWheelWireframePipeline(VkRenderPass renderPass);
    void CreateBlankDepthPipeline(VkRenderPass renderPass);

    void CreatePlaneInjectionPipeline(VkRenderPass renderPass);
    void CreateGrindingWheelEdgePipeline(VkRenderPass renderPass);

private:
    LveDevice& m_lveDevice;
    VkPipelineLayout m_pipelineLayout;

    // 棒料 (Blank) - 普通管线
    std::unique_ptr<LvePipeline> m_blankStencilPipeline;
    std::unique_ptr<LvePipeline> m_blankColorPipeline;  

    // 砂轮 (Wheel) - 实例管线
    std::unique_ptr<LvePipeline> m_grndWheelStencilFrontPipeline;
    std::unique_ptr<LvePipeline> m_grndWheelStencilBackPipeline;

    // 砂轮 (Wheel) - 线框管线
    std::unique_ptr<LvePipeline> m_grndWheelWireframePipeline;

    std::unique_ptr<LvePipeline> m_blankDepthPipeline;

    std::unique_ptr<LvePipeline> m_planeInjectionPipeline;

    std::unique_ptr<LvePipeline> m_grndWheelEdgePipeline;
};

}  // namespace lve
