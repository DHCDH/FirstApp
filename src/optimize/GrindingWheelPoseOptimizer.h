#pragma once

#include <glm.hpp>
#include <vector>

#include "LveModel.h"
#include "integration/AdaptiveSimpsonStrategy.h"
#include "integration/NumericalIntegrator.h"
#include "OptimizeResourceContext.h"
#include "OptimizeMaskRenderSystem.h"
#include "OptimizePoseRenderSystem.h"
#include "ArcProjectionSolver.h"

namespace optimize
{

class GrindingWheelPoseOptimizer
{
public:
    GrindingWheelPoseOptimizer(lve::LveDevice& device, lve::LveModel& blank,
                               lve::LveModel& grndWheel);
    ~GrindingWheelPoseOptimizer();

    GrindingWheelPoseOptimizer(const GrindingWheelPoseOptimizer&) = delete;
    GrindingWheelPoseOptimizer& operator=(const GrindingWheelPoseOptimizer&) = delete;

    void InitializeDataForPSO(OptimizeResourceContext& context);
    void RunOptimization(OptimizeResourceContext& context,
                         OptimizeMaskRenderSystem& maskRenderSystem,
                         const SliceViewConfig& viewConfig, Plane plane,
                         BatchedWheelPushConstants wheelPushData);

    // --- 供外部获取最终的最优结果 ---
    inline glm::mat4 GetBestPose() const
    {
        return m_bestPose;
    }
    inline float GetBestScore() const
    {
        return m_bestScore;
    }
    VkDescriptorSetLayout GetSSBODescriptorSetLayout() const
    {
        return m_SSBOSetLayout->GetDescriptorSetLayout();
    }

private:
    void InsertComputeBarrier(VkCommandBuffer cmd);

    void WriteToolPath();

    std::vector<Triangle> ExtractTriangles(const lve::LveModel& model);

    void ReadBackBestResult(const OptimizeResourceContext& context);

private:
    ArcProjectionSolver m_arcProjectionSolver;

    std::vector<PoseData> m_poseData;

    PoseConstants m_poseConstants;
    std::vector<Particle> m_swarm;

    lve::LveDevice& m_lveDevice;
    lve::LveModel& m_blank;
    lve::LveModel& m_grndWheel;

    // --- SSBO ---
    std::unique_ptr<lve::LveBuffer> m_poseSSBOBuffer;
    std::unique_ptr<lve::LveDescriptorPool> m_descriptorPool;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_SSBOSetLayout;
    VkDescriptorSet m_SSBODescriptorSet;

    glm::mat4 m_bestPose{1.0f};
    BestResultData m_bestResult{};
    float m_bestScore{-999999.0f};

    uint32_t numTriangle{0};
};

}  // namespace optimize