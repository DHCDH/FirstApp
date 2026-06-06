#include "CpuSdfGenerator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace cpu
{

float SdfImage::Sample(float u, float v) const
{
    if (data.empty()) return std::numeric_limits<float>::max();

    float colF = u * static_cast<float>(width);
    float rowF = v * static_cast<float>(height);

    uint32_t col0 = static_cast<uint32_t>(colF);
    uint32_t row0 = static_cast<uint32_t>(rowF);
    uint32_t col1 = std::min(col0 + 1, width - 1);
    uint32_t row1 = std::min(row0 + 1, height - 1);

    float fx = colF - static_cast<float>(col0);
    float fy = rowF - static_cast<float>(row0);

    float v00 = (*this)(row0, col0);
    float v10 = (*this)(row0, col1);
    float v01 = (*this)(row1, col0);
    float v11 = (*this)(row1, col1);

    return (1.f - fy) * ((1.f - fx) * v00 + fx * v10) +
           fy * ((1.f - fx) * v01 + fx * v11);
}

bool CpuSdfGenerator::ParseFlutePointSet(const std::string& filePath)
{
    m_flutePoints.clear();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // 跳过注释行、new seg、Plane 等无效行
        if (line.front() == '#' || line.find("new seg") != std::string::npos ||
            line.find("Plane") != std::string::npos) {
            continue;
        }

        // 将逗号替换为空格，方便流读取
        for (char& ch : line) {
            if (ch == ',') ch = ' ';
        }

        // 提取 y, z 浮点数（文件格式为每行 y, z）
        std::stringstream ss(line);
        float y, z;
        if (ss >> y >> z) {
            m_flutePoints.emplace_back(0.f, y, z, 1.f);
        }
    }

    file.close();
    return !m_flutePoints.empty();
}

glm::vec4 CpuSdfGenerator::CalculateSdfMapInfo(uint32_t width, uint32_t height) const
{
    if (m_flutePoints.empty()) {
        return glm::vec4(0.f);
    }

    constexpr float kMaxFloat = std::numeric_limits<float>::max();
    float minY = kMaxFloat, maxY = -kMaxFloat;
    float minZ = kMaxFloat, maxZ = -kMaxFloat;

    for (const auto& pt : m_flutePoints) {
        minZ = std::min(minZ, pt.z);
        maxZ = std::max(maxZ, pt.z);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }

    float centerZ = (minZ + maxZ) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;

    float rangeZ = maxZ - minZ;
    float rangeY = maxY - minY;
    float maxSide = std::max(rangeZ, rangeY) * 1.2f;

    float dx = maxSide / static_cast<float>(width);
    float dz = maxSide / static_cast<float>(height);

    return glm::vec4(centerZ - maxSide * 0.5f, centerY - maxSide * 0.5f, dx, dz);
}

void CpuSdfGenerator::Generate(uint32_t width, uint32_t height,
                                const SdfGenConfig& config)
{
    m_sdf.width = width;
    m_sdf.height = height;
    m_sdf.data.resize(static_cast<size_t>(width) * height);
    m_sdf.targetPoint = config.targetPoint;
    m_sdf.xMin = config.xMin;
    m_sdf.zMax = config.zMax;
    m_sdf.dx = config.dx;
    m_sdf.dy = config.dy;

    const size_t pointCount = m_flutePoints.size();

    for (uint32_t row = 0; row < height; ++row) {
        // physY = targetPoint.y + zMax - (row + 0.5) * dy
        float physY = config.targetPoint.y +
                      config.zMax - (static_cast<float>(row) + 0.5f) * config.dy;

        for (uint32_t col = 0; col < width; ++col) {
            // physZ = targetPoint.z + xMin + (col + 0.5) * dx
            float physZ = config.targetPoint.z +
                          config.xMin + (static_cast<float>(col) + 0.5f) * config.dx;

            float minDistSq = 1e10f;
            for (size_t i = 0; i < pointCount; ++i) {
                float dZ = physZ - m_flutePoints[i].z;
                float dY = physY - m_flutePoints[i].y;
                float d2 = dZ * dZ + dY * dY;
                if (d2 < minDistSq) {
                    minDistSq = d2;
                }
            }

            m_sdf(row, col) = std::sqrt(minDistSq);
        }
    }
}

}  // namespace cpu
