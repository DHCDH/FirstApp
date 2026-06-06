#include "CpuWearInversion.h"

#include <limits>

namespace cpu
{

bool CpuWearInversion::PrepareSdf(const std::string& fluteFilePath)
{
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

    return true;
}

float CpuWearInversion::EvaluatePoints(const std::vector<glm::vec3>& points) const
{
    std::vector<glm::vec2> points2D;
    points2D.reserve(points.size());
    for (const auto& p : points) {
        points2D.emplace_back(p.y, p.z);
    }
    return m_lossCalculator.ComputeLoss(points2D, m_sdfGenerator.GetSdf());
}

float CpuWearInversion::RunOptimization(
    std::function<std::vector<glm::vec3>(float)> pointProvider)
{
    const SdfImage& sdf = m_sdfGenerator.GetSdf();

    float bestLoss = std::numeric_limits<float>::max();
    uint32_t iterCount = 0;

    auto evalLoss = [&](float r) -> float {
        ++iterCount;
        std::vector<glm::vec3> points = pointProvider(r);

        std::vector<glm::vec2> points2D;
        points2D.reserve(points.size());
        for (const auto& p : points) {
            points2D.emplace_back(p.y, p.z);
        }

        float loss = m_lossCalculator.ComputeLoss(points2D, sdf);
        if (loss < bestLoss) {
            bestLoss = loss;
            m_bestRadius = r;
        }
        return loss;
    };

    m_fitter.FitBrent(m_fitConfig, evalLoss);

    m_bestLoss = bestLoss;
    m_iterationCount = iterCount;
    return m_bestRadius;
}

}  // namespace cpu
