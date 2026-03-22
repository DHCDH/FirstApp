#pragma once

#include <memory>
#include <vector>

#include "../Global.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LvePipeline.h"

struct BatchedWheelPushConstants {
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;

    float stepZ;
    float tanHelixAngle;
    float radius;
    int stepsPerPose;
};

class GrindingWheelPoseRenderSystem
{
public:
    GrindingWheelPoseRenderSystem(lve::LveDevice& device, VkRenderPass renderPass,
                                  VkDescriptorSetLayout globalSetLayout,
                                  VkDescriptorSetLayout ssboSetLayout);
    ~GrindingWheelPoseRenderSystem();

    GrindingWheelPoseRenderSystem(const GrindingWheelPoseRenderSystem&) = delete;
    GrindingWheelPoseRenderSystem& operator=(const GrindingWheelPoseRenderSystem&) =
        delete;

    // 基于砂轮初始位姿获取刀轨
    void RenderBatchWheels(VkCommandBuffer commandBuffer,
                           const BatchedWheelPushConstants& pushData,
                           uint32_t instanceCount, VkDescriptorSet globalDescriptorSet);

private:
    void CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout,
                              VkDescriptorSetLayout ssboSetLayout);
    void CreateStencilFrontPipeline(VkRenderPass renderPass);
    void CreateStencilBackPipeline(VkRenderPass renderPass);
    void CreateStencilResolvePipeline(VkRenderPass renderPass);
    void CreateStencilClearPipeline(VkRenderPass renderPass);

    lve::LveDevice& lveDevice;
    VkPipelineLayout m_pipelineLayout;
    std::unique_ptr<LvePipeline> m_stencilFrontPipeline;
    std::unique_ptr<LvePipeline> m_stencilBackPipeline;
    std::unique_ptr<LvePipeline> m_stencilResolvePipeline;
    std::unique_ptr<LvePipeline> m_stencilClearPipeline;
    

    lve::LveModel& m_grndWheelModel;
    uint32_t m_instanceCount;

};