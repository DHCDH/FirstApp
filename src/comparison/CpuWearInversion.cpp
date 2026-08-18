#include "CpuWearInversion.h"

#include "CalculateFlute.h"
#include "CalculateGrindingWheelMaxRadius.h"

#include <chrono>
#include <iostream>
#include <limits>

namespace cpu
{

bool CpuWearInversion::PrepareSdf(const std::string& fluteFilePath)
{
    auto t0 = std::chrono::high_resolution_clock::now();

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

    auto t1 = std::chrono::high_resolution_clock::now();
    m_sdfTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    return true;
}

float CpuWearInversion::EvaluatePoints(const std::vector<glm::vec3>& points) const
{
    std::vector<glm::vec2> points2D;
    points2D.reserve(points.size());
    for (const auto& p : points) {
        // SDF 由原始 flute_1V1_r0.1.txt 生成，其格式为: 列1=-p.y, 列2=-p.x
        // ParseFlutePointSet 读入后: point.z=列1, point.y=列2
        // SDF水平轴(physZ)对point.z=刀具Y, SDF垂直轴(physY)对point.y=刀具X
        // 因此 2D 点应映射为: x→physY→刀具X, y→physZ→刀具Y
        points2D.emplace_back(-p.x, -p.y);
    }
    return m_lossCalculator.ComputeLoss(points2D, m_sdfGenerator.GetSdf());
}

float CpuWearInversion::EvaluateRadius(float r)
{
    ++m_iterationCount;
    WheelParams gp = m_wheelParams;
    gp.gr1 = r;
    gp.gr2 = r;
    CalculateFlute calc(gp, m_toolpaths);
    std::vector<glm::vec3> points = calc.CreateFlute();

    float loss = EvaluatePoints(points);
    std::cout << "  [iter " << m_iterationCount << "] r = " << r
              << " mm, loss = " << loss << std::endl;
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
    m_wheelParams = baseParams;
    m_toolpaths = toolpaths;

    m_bestLoss = std::numeric_limits<float>::max();
    m_iterationCount = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    m_fitter.FitBrent(m_fitConfig, EvalLossStatic, this);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto optMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    double totalMs = m_sdfTimeMs + optMs;

    std::cout << "\n========== CPU Wear Inversion Result ==========\n";
    std::cout << "  Best corner radius: " << m_bestRadius << " mm\n";
    std::cout << "  Best loss:          " << m_bestLoss << "\n";
    std::cout << "  Iterations:         " << m_iterationCount << "\n";
    std::cout << "  ----------------------------------------------\n";
    std::cout << "  SDF generation:     " << m_sdfTimeMs << " ms\n";
    std::cout << "  Optimization:       " << optMs << " ms  (" << (m_iterationCount > 0 ? optMs / m_iterationCount : 0.0) << " ms/iter)\n";
    std::cout << "  Total CPU time:     " << totalMs << " ms\n";
    std::cout << "================================================\n"
              << std::endl;

    return m_bestRadius;
}

}  // namespace cpu
