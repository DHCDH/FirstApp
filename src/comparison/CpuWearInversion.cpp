#include "CpuWearInversion.h"

#include "CalculateFlute.h"
#include "CalculateGrindingWheelMaxRadius.h"

#include <limits>

namespace cpu
{

bool CpuWearInversion::PrepareSdf(const std::string& fluteFilePath)
{
    PROFILE_SCOPE("CpuWearInversion::PrepareSdf");
    if (!m_sdfGenerator.ParseFlutePointSet(fluteFilePath)) {
        return false;
    }

    glm::vec4 mapInfo = m_sdfGenerator.CalculateSdfMapInfo(m_sdfWidth, m_sdfHeight);
    float physSize = static_cast<float>(m_sdfWidth) * mapInfo.z;
    float centerZ = mapInfo.x + physSize * 0.5f;
    float centerY = mapInfo.y + physSize * 0.5f;
    glm::vec3 targetPoint(0.f, centerY, centerZ);

    m_viewConfig.xMin = -physSize * 0.5f;
    m_viewConfig.xMax = physSize * 0.5f;
    m_viewConfig.zMin = -physSize * 0.5f;
    m_viewConfig.zMax = physSize * 0.5f;
    m_viewConfig.nX = m_sdfWidth;
    m_viewConfig.nZ = m_sdfHeight;

    SdfGenConfig sdfConfig;
    sdfConfig.targetPoint = targetPoint;
    sdfConfig.xMin = m_viewConfig.xMin;
    sdfConfig.zMax = m_viewConfig.zMax;
    sdfConfig.dx = (m_viewConfig.xMax - m_viewConfig.xMin) / static_cast<float>(m_sdfWidth);
    sdfConfig.dy = (m_viewConfig.zMax - m_viewConfig.zMin) / static_cast<float>(m_sdfHeight);
    m_sdfGenerator.Generate(m_sdfWidth, m_sdfHeight, sdfConfig);

    m_sdfGenerator.ExportSdf(
        "D:/Data/Study/vulkan/FirstApp/output_stuff/wear/Cpu_comparison");

    return true;
}

float CpuWearInversion::EvaluatePoints(const std::vector<glm::vec3>& points) const
{
    PROFILE_SCOPE("CpuWearInversion::EvaluatePoints");
    std::vector<glm::vec2> points2D;
    points2D.reserve(points.size());
    for (const auto& p : points) {
        points2D.emplace_back(p.y, p.z);
    }
    return m_lossCalculator.ComputeLoss(points2D, m_sdfGenerator.GetSdf());
}

float CpuWearInversion::EvaluateRadius(float r)
{
    PROFILE_SCOPE("CpuWearInversion::EvaluateRadius");
    ++m_iterationCount;
    WheelParams gp = m_wheelParams;
    gp.gr1 = r;
    gp.gr2 = r;
    CalculateFlute calc(gp, m_toolpaths);
    std::vector<glm::vec3> points = calc.CreateFlute();

    float loss = EvaluatePoints(points);
    if (loss < m_bestLoss) {
        m_bestLoss = loss;
        m_bestRadius = r;
    }
    return loss;
}

float CpuWearInversion::EvalLossStatic(float r, void* ctx)
{
    return static_cast<CpuWearInversion*>(ctx)->EvaluateRadius(r);
}

float CpuWearInversion::RunOptimization(
    const WheelParams& baseParams,
    const std::vector<ToolPath>& toolpaths)
{
    PROFILE_SCOPE("CpuWearInversion::RunOptimization");
    m_wheelParams = baseParams;
    m_toolpaths = toolpaths;

    m_bestLoss = std::numeric_limits<float>::max();
    m_iterationCount = 0;

    m_fitter.FitBrent(m_fitConfig, EvalLossStatic, this);

    return m_bestRadius;
}

}  // namespace cpu
