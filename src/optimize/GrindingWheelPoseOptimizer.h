#pragma once

#include <glm.hpp>
#include <vector>

#include "LveModel.h"
#include "integration/AdaptiveSimpsonStrategy.h"
#include "integration/NumericalIntegrator.h"
#include "OptimizeResourceContext.h"
#include "OptimizeMaskRenderSystem.h"
#include "OptimizePoseRenderSystem.h"

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
    void InitSSBOResources();
    void CalculateTransformMatrixes();
    // 根据侦察到的 BBox 算出局部放大的相机
    CameraData CalculateMicroCamera(const BBoxData& bbox,
                                    const SliceViewConfig& macroConfig, uint32_t texWidth,
                                    uint32_t texHeight, const Plane& plane);
    float EvaluateFitness(const ResultData& result);

    void WriteToolPath();

    std::vector<Triangle> ExtractTriangles(const lve::LveModel& model);

private:
    std::vector<PoseData> m_poseData;

    lve::LveDevice& m_lveDevice;
    lve::LveModel& m_blank;
    lve::LveModel& m_grndWheel;

    // --- SSBO ---
    std::unique_ptr<lve::LveBuffer> m_poseSSBOBuffer;
    std::unique_ptr<lve::LveDescriptorPool> m_descriptorPool;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_SSBOSetLayout;
    VkDescriptorSet m_SSBODescriptorSet;

    glm::mat4 m_bestPose{1.0f};
    ResultData m_bestResult{};
    float m_bestScore{-999999.0f};
};

}  // namespace optimize