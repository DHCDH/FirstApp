#pragma once

#include <memory>
#include <vector>

#include "../Global.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LvePipeline.h"

namespace optimize
{
struct OptimizeDrawInfo {
    VkCommandBuffer commandBuffer;
    lve::LveModel& model;
    glm::mat4 modelMatrix;
    VkDescriptorSet globalDescriptorSet;  // (绑定CameraSSBO)
    glm::vec3 normal;
    glm::vec3 point;
};

struct OptimizePlaneInfo {
    VkCommandBuffer commandBuffer;
    VkDescriptorSet globalDescriptorSet;
    glm::vec3 normal;
    glm::vec3 point;
};

class OptimizeMaskRenderSystem
{
public:
    OptimizeMaskRenderSystem(lve::LveDevice& device, VkRenderPass renderPass,
                             VkDescriptorSetLayout graphicsSetLayout,
                             VkDescriptorSetLayout computeSetLayout,
                             VkDescriptorSetLayout bboxSetLayout);
    ~OptimizeMaskRenderSystem();

    OptimizeMaskRenderSystem(const OptimizeMaskRenderSystem&) = delete;
    OptimizeMaskRenderSystem& operator=(const OptimizeMaskRenderSystem&) = delete;

    void RenderPlaneInjection(const OptimizePlaneInfo& info);
    void RenderBlank(const OptimizeDrawInfo& info);

    void ComputeBBox(VkCommandBuffer commandBuffer, VkDescriptorSet bboxDescriptorSet,
                     uint32_t width, uint32_t height, uint32_t planeIdx = 0);

    void ComputeFlute(VkCommandBuffer commandBuffer, const SliceComputeInfo& computeInfo,
                      VkDescriptorSet globalDescriptorSet, lve::LveBuffer* tipInfoBuffer);

    void BindPlaneInjectionPipeline(VkCommandBuffer commandBuffer);
    void BindBlankStencilPipeline(VkCommandBuffer commandBuffer);
    void BindBlankColorPipeline(VkCommandBuffer commandBuffer);

private:
    void CreatePipelineLayout(const VkDescriptorSetLayout& graphicsSetLayout);
    void CreateComputePipelineLayout(const VkDescriptorSetLayout& computeSetLayout,
                                     const VkDescriptorSetLayout& graphicsSetLayout);
    void CreateBBoxPipelineLayout(const VkDescriptorSetLayout& bboxSetLayout);

    void CreateBlankStencilPipeline(VkRenderPass renderPass);
    void CreateBlankColorPipeline(VkRenderPass renderPass);
    void CreatePlaneInjectionPipeline(VkRenderPass renderPass);
    void CreateComputePipeline();
    void CreateBBoxPipeline();

private:
    lve::LveDevice& m_lveDevice;

    // 图形管线
    VkPipelineLayout m_pipelineLayout;
    std::unique_ptr<lve::LvePipeline> m_blankStencilPipeline;
    std::unique_ptr<lve::LvePipeline> m_blankColorPipeline;
    std::unique_ptr<lve::LvePipeline> m_planeInjectionPipeline;

    // Compute 管线
    VkPipelineLayout m_computePipelineLayout;
    std::unique_ptr<lve::LvePipeline> m_extractContourPipeline;
    std::unique_ptr<lve::LvePipeline> m_knnPipeline;
    std::unique_ptr<lve::LvePipeline> m_tracePipeline;
    std::unique_ptr<lve::LvePipeline> m_alignPipeline;
    std::unique_ptr<lve::LvePipeline> m_rakeAnglePipeline;

    // BBox 专属管线
    VkPipelineLayout m_bboxPipelineLayout;
    std::unique_ptr<lve::LvePipeline> m_bboxPipeline;
};

}  // namespace optimize