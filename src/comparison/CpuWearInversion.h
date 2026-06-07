#pragma once

#include <string>
#include <vector>

#include "../Global.h"
#include "CpuLossCalculator.h"
#include "CpuSdfGenerator.h"
#include "CpuWearFitter.h"

namespace cpu
{

class CpuWearInversion
{
public:
    CpuWearInversion() = default;
    ~CpuWearInversion() = default;

    void SetFitConfig(const FitConfig& config) { m_fitConfig = config; }
    void SetViewResolution(uint32_t width, uint32_t height)
    {
        m_sdfWidth = width;
        m_sdfHeight = height;
    }

    // 加载目标容屑槽点文件并生成SDF距离场
    bool PrepareSdf(const std::string& fluteFilePath);

    float RunOptimization(const WheelParams& baseParams,
                          const std::vector<ToolPath>& toolpaths);

    // 直接评估一组点的Loss
    float EvaluatePoints(const std::vector<glm::vec3>& points) const;

    // 结果查询
    float GetBestCornerRadius() const { return m_bestRadius; }
    float GetBestLoss() const { return m_bestLoss; }
    const SdfImage& GetSdfImage() const { return m_sdfGenerator.GetSdf(); }
    uint32_t GetIterationCount() const { return m_iterationCount; }
    const ViewConfig& GetViewConfig() const { return m_viewConfig; }

private:
    CpuSdfGenerator m_sdfGenerator;
    CpuLossCalculator m_lossCalculator;
    CpuWearFitter m_fitter;

    FitConfig m_fitConfig;
    uint32_t m_sdfWidth = 1024;
    uint32_t m_sdfHeight = 1024;
    ViewConfig m_viewConfig;

    float EvaluateRadius(float r);
    static float EvalLossStatic(float r, void* ctx);

    WheelParams m_wheelParams;
    std::vector<ToolPath> m_toolpaths;

    float m_bestRadius = 0.f;
    float m_bestLoss = 0.f;
    uint32_t m_iterationCount = 0;
};

}  // namespace cpu
