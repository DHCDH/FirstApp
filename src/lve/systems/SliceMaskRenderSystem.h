#pragma once

#include <memory>
#include <vector>

#include "../Global.h"
#include "LveCamera.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "LvePipeline.h"

struct SliceMaskRenderPassData {
    VkRenderPass renderPass;
    uint32_t width;
    uint32_t height;

    VkFramebuffer frameBuffer;

    VkDescriptorSet globalDescriptorSet;
    lve::LveBuffer* grndWheelInstancesBuffer;
    uint32_t grndWheelInstancesCount;
};

namespace lve
{
class SliceMaskRenderSystem
{
public:
    SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass,
                          VkDescriptorSetLayout graphicsSetLayouts,
                          VkDescriptorSetLayout computeSetLayouts,
                          VkDescriptorSetLayout bboxSetLayout);
    ~SliceMaskRenderSystem();

    SliceMaskRenderSystem(const SliceMaskRenderSystem&) = delete;
    SliceMaskRenderSystem& operator=(const SliceMaskRenderSystem&) = delete;

    void RenderBlank(const SliceDrawInfo& sliceMaskInfo);
    void RenderGrindingWheelInstances(const SliceInstancedInfo& info,
                                      uint32_t firstInstance = 0);
    void RenderPlaneInjection(const SlicePlaneInfo& info);
    void RenderSliceContour(const SliceInstancedInfo& info);

    void BindBlankStencilPipeline(VkCommandBuffer commandBuffer);
    void BindBlankColorPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelStencilFrontPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelStencilBackPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelWireframePipeline(VkCommandBuffer commandBuffer);
    void BindBlankDepthPipeline(VkCommandBuffer commandBuffer);
    void BindStencilResolvePipeline(VkCommandBuffer commandBuffer);
    void BindStencilClearPipeline(VkCommandBuffer commandBuffer);

    void BindPlaneInjectionPipeline(VkCommandBuffer commandBuffer);
    void BindGrindingWheelEdgePipeline(VkCommandBuffer commandBuffer);

    void BindSliceContourPipeline(VkCommandBuffer commandBuffer);

    // 计算砂轮截形外轮廓的几何着色器管线
    void CreateSliceContourPipeline(VkRenderPass renderPass);

    // 整合渲染逻辑
    void RenderMask(VkCommandBuffer commandBuffer,
                    const SliceMaskRenderPassData& renderPassData,
                    const RasterizerData& rasData, Plane plane);

    // 整合计算逻辑
    void ComputeFlute(VkCommandBuffer commandBuffer, SliceComputeInfo computeInfo,
                      LveBuffer* tipInfoBuffer);

    void ComputeBBox(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet,
                     uint32_t width, uint32_t height, uint32_t planeIdx);

private:
    LveDevice& m_lveDevice;

private:
    // --- 图形资源 ---
    VkPipelineLayout m_pipelineLayout;

    // 棒料 (Blank) - 普通管线
    std::unique_ptr<LvePipeline> m_blankStencilPipeline;
    std::unique_ptr<LvePipeline> m_blankColorPipeline;
    std::unique_ptr<LvePipeline> m_blankDepthPipeline;

    // 砂轮 (Wheel) - 实例管线
    std::unique_ptr<LvePipeline> m_grndWheelStencilFrontPipeline;
    std::unique_ptr<LvePipeline> m_grndWheelStencilBackPipeline;

    // 砂轮 (Wheel) - 线框管线
    std::unique_ptr<LvePipeline> m_grndWheelWireframePipeline;

    std::unique_ptr<LvePipeline> m_planeInjectionPipeline;

    std::unique_ptr<LvePipeline> m_grndWheelEdgePipeline;

    // 计算轮廓几何着色器管线
    std::unique_ptr<LvePipeline> m_sliceContourPipeline;

    std::unique_ptr<LvePipeline> m_stencilResolvePipeline;  // 固化Stencil到Color
    std::unique_ptr<LvePipeline> m_stencilClearPipeline;    // 清空Stencil

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

    void CreateStencilResolvePipeline(VkRenderPass renderPass);
    void CreateStencilClearPipeline(VkRenderPass renderPass);

    void CreateComputePipelineLayout(const VkDescriptorSetLayout& setLayout);
    void CreateComputePipeline();

    void CreateBBoxPipeline(VkDescriptorSetLayout descriptorSetLayout);

private:
    // --- 计算资源 ---

    // 提取交集轮廓点
    VkPipelineLayout m_computePipelineLayout;
    std::unique_ptr<LvePipeline> m_extractContourPipeline;

    // 轮廓点排序
    std::unique_ptr<LvePipeline> m_knnPipeline;
    std::unique_ptr<LvePipeline> m_tracePipeline;
    std::unique_ptr<LvePipeline> m_alignPipeline;

    // 计算前角
    std::unique_ptr<LvePipeline> m_rakeAnglePipeline;

    VkPipelineLayout m_bboxPipelineLayout;
    std::unique_ptr<LvePipeline> m_bboxPipeline;
};

}  // namespace lve
