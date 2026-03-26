#pragma once

#include <memory>
#include <vector>

#include "../Global.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LvePipeline.h"

namespace optimize
{
struct BatchedWheelPushConstants {
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;

    alignas(16) float stepX;
    float tanHelixAngle;
    float radius;
    int stepsPerPose;
};

class OptimizePoseRenderSystem
{
public:
    OptimizePoseRenderSystem(lve::LveDevice& device, lve::LveModel& grndWheelModel,
                             VkRenderPass renderPass,
                             VkDescriptorSetLayout globalSetLayout,
                             VkDescriptorSetLayout ssboSetLayout);
    ~OptimizePoseRenderSystem();

    OptimizePoseRenderSystem(const OptimizePoseRenderSystem&) = delete;
    OptimizePoseRenderSystem& operator=(const OptimizePoseRenderSystem&) = delete;

    // 基于砂轮初始位姿获取刀轨
    void RenderBatchWheels(VkCommandBuffer commandBuffer,
                           const BatchedWheelPushConstants& pushData,
                           uint32_t instanceCount, VkDescriptorSet globalDescriptorSet,
                           VkDescriptorSet ssboDescriptorSet);

private:
    void CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout,
                              VkDescriptorSetLayout ssboSetLayout);
    void CreateStencilFrontPipeline(VkRenderPass renderPass);
    void CreateStencilBackPipeline(VkRenderPass renderPass);
    void CreateStencilResolvePipeline(VkRenderPass renderPass);
    void CreateStencilClearPipeline(VkRenderPass renderPass);

    lve::LveDevice& m_lveDevice;
    VkPipelineLayout m_pipelineLayout;
    std::unique_ptr<lve::LvePipeline> m_stencilFrontPipeline;
    std::unique_ptr<lve::LvePipeline> m_stencilBackPipeline;
    std::unique_ptr<lve::LvePipeline> m_stencilResolvePipeline;
    std::unique_ptr<lve::LvePipeline> m_stencilClearPipeline;

    lve::LveModel& m_grndWheelModel;
    uint32_t m_instanceCount;
};

}  // namespace optimize