#pragma once

#include <glm.hpp>
#include <memory>
#include <vector>

#include "../Global.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LvePipeline.h"

namespace optimize
{
struct Triangle {
    alignas(16) glm::vec4 v0;
    alignas(16) glm::vec4 v1;
    alignas(16) glm::vec4 v2;
};

// 2. 对应 Shader 里的 Push Constants
struct PolarPushConstants {
    alignas(16) glm::vec3 planeNormal;
    alignas(16) glm::vec3 planePoint;
    alignas(16) glm::vec3 planeU;
    alignas(16) glm::vec3 planeV;

    alignas(16) float stepX;
    float tanHelixAngle;
    float radius;
    uint32_t stepsPerPose;
    uint32_t numTriangles;
    uint32_t curBatchSize;

    glm::vec3 rt1;
    float u1;
    glm::vec3 nt;
    float pad_{0.};
    float gR;
    float gr1;
    float gw;

    float pad2_{0.};
};

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
    OptimizeMaskRenderSystem(lve::LveDevice& device,
                             VkDescriptorSetLayout computeSetLayout);
    ~OptimizeMaskRenderSystem();

    OptimizeMaskRenderSystem(const OptimizeMaskRenderSystem&) = delete;
    OptimizeMaskRenderSystem& operator=(const OptimizeMaskRenderSystem&) = delete;

public:
    void CreateComputePipelineLayout(
        const VkDescriptorSetLayout& computeSetLayout);  // mark

    void CreateComputePipelines();  // mark

    void ComputePolarPSOUpdate(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                          const PolarPushConstants& push);
    void ComputePolarIntersect(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                               const PolarPushConstants& push);  // mark
    void ComputePolarEvaluate(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                              const PolarPushConstants& push);
    void ComputePolarReduce(VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                            const PolarPushConstants& push);

private:
    lve::LveDevice& m_lveDevice;

    // Compute 管线
    VkPipelineLayout m_computePipelineLayout;  // mark

    std::unique_ptr<lve::LvePipeline> m_polarPSOPipeline;
    std::unique_ptr<lve::LvePipeline> m_polarIntersectPipeline;
    std::unique_ptr<lve::LvePipeline> m_polarEvaluatePipeline;
    std::unique_ptr<lve::LvePipeline> m_polarReducePipeline;
};

}  // namespace optimize