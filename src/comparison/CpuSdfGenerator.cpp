#include "CpuSdfGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

#include "../Global.h"
#include "Logger.h"

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

    m_flutePoints.reserve(4096);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        if (line.front() == '#' || line.find("new seg") != std::string::npos ||
            line.find("Plane") != std::string::npos) {
            continue;
        }

        // 使用 strtof 替代 stringstream，避免每次分配
        const char* p = line.c_str();
        char* end = nullptr;
        // 文件第1列 = -p.x, 第2列 = -p.y
        // point.z 对应 SDF 水平轴 (X轴), point.y 对应 SDF 垂直轴 (Y轴)
        float z = std::strtof(p, &end);
        if (end == p) continue;

        p = end;
        while (*p == ',' || *p == ' ') ++p;

        float y = std::strtof(p, &end);

        m_flutePoints.emplace_back(0.f, y, z, 1.f);
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
    if (pointCount == 0) return;

    const float physWidth = config.dx * static_cast<float>(width);
    const float physHeight = config.dy * static_cast<float>(height);
    const float z0 = config.targetPoint.z + config.xMin;
    const float y0 = config.targetPoint.y + config.zMax - physHeight;

    // --- 构建空间网格索引 ---
    constexpr int GRID_SIZE = 32;
    std::vector<std::vector<size_t>> grid(GRID_SIZE * GRID_SIZE);
    const float invCellW = static_cast<float>(GRID_SIZE) / physWidth;
    const float invCellH = static_cast<float>(GRID_SIZE) / physHeight;

    for (size_t i = 0; i < pointCount; ++i) {
        int gx = static_cast<int>((m_flutePoints[i].z - z0) * invCellW);
        int gy = static_cast<int>((m_flutePoints[i].y - y0) * invCellH);
        gx = std::clamp(gx, 0, GRID_SIZE - 1);
        gy = std::clamp(gy, 0, GRID_SIZE - 1);
        grid[static_cast<size_t>(gy) * GRID_SIZE + static_cast<size_t>(gx)].push_back(i);
    }

    // 预计算每列的 physZ 和 gridX
    std::vector<float> colPhysZ(width);
    std::vector<int> colGridX(width);
    for (uint32_t col = 0; col < width; ++col) {
        colPhysZ[col] = config.targetPoint.z +
                        config.xMin + (static_cast<float>(col) + 0.5f) * config.dx;
        colGridX[col] = std::clamp(
            static_cast<int>((colPhysZ[col] - z0) * invCellW), 0, GRID_SIZE - 1);
    }

    // 多线程并行
    const unsigned int numThreads =
        std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    const uint32_t rowsPerThread = (height + numThreads - 1) / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (unsigned int t = 0; t < numThreads; ++t) {
        uint32_t startRow = t * rowsPerThread;
        uint32_t endRow = std::min(startRow + rowsPerThread, height);
        if (startRow >= endRow) break;

        threads.emplace_back([this, width, startRow, endRow, &config,
                              &grid, &colPhysZ, &colGridX, z0, y0,
                              invCellW, invCellH]() {
            for (uint32_t row = startRow; row < endRow; ++row) {
                const float physY = config.targetPoint.y +
                                    config.zMax -
                                    (static_cast<float>(row) + 0.5f) * config.dy;
                const int gridY = std::clamp(
                    static_cast<int>((physY - y0) * invCellH), 0, GRID_SIZE - 1);

                for (uint32_t col = 0; col < width; ++col) {
                    const float physZ = colPhysZ[col];
                    const int gridX = colGridX[col];

                    float minDistSq = 1e10f;

                    // 搜索 5x5 邻域网格单元
                    const int gyMin = std::max(0, gridY - 2);
                    const int gyMax = std::min(GRID_SIZE - 1, gridY + 2);
                    const int gxMin = std::max(0, gridX - 2);
                    const int gxMax = std::min(GRID_SIZE - 1, gridX + 2);

                    for (int gy = gyMin; gy <= gyMax; ++gy) {
                        for (int gx = gxMin; gx <= gxMax; ++gx) {
                            for (size_t i :
                                 grid[static_cast<size_t>(gy) * GRID_SIZE +
                                      static_cast<size_t>(gx)]) {
                                const float dZ =
                                    physZ - m_flutePoints[i].z;
                                const float dY =
                                    physY - m_flutePoints[i].y;
                                const float d2 = dZ * dZ + dY * dY;
                                if (d2 < minDistSq) {
                                    minDistSq = d2;
                                }
                            }
                        }
                    }

                    m_sdf(row, col) = std::sqrt(minDistSq);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }
}

bool CpuSdfGenerator::ExportSdf(const std::string& dirPath) const
{
    if (m_sdf.data.empty()) {
        ERROR("ExportSdf: no SDF data to export");
        return false;
    }

    std::filesystem::create_directories(dirPath);

    // JSON元数据
    std::string metaPath = dirPath + "/sdf_meta.json";
    std::ofstream meta(metaPath);
    if (!meta.is_open()) {
        ERROR("ExportSdf: failed to open %s", metaPath.c_str());
        return false;
    }
    meta << "{\n";
    meta << "  \"width\": " << m_sdf.width << ",\n";
    meta << "  \"height\": " << m_sdf.height << ",\n";
    meta << "  \"targetPoint\": [" << m_sdf.targetPoint.x << ", "
         << m_sdf.targetPoint.y << ", " << m_sdf.targetPoint.z << "],\n";
    meta << "  \"xMin\": " << m_sdf.xMin << ",\n";
    meta << "  \"zMax\": " << m_sdf.zMax << ",\n";
    meta << "  \"dx\": " << m_sdf.dx << ",\n";
    meta << "  \"dy\": " << m_sdf.dy << "\n";
    meta << "}\n";
    meta.close();

    // 二进制距离场数据 (float32, row-major)
    std::string dataPath = dirPath + "/sdf_data.bin";
    std::ofstream data(dataPath, std::ios::binary);
    if (!data.is_open()) {
        ERROR("ExportSdf: failed to open %s", dataPath.c_str());
        return false;
    }
    data.write(reinterpret_cast<const char*>(m_sdf.data.data()),
               m_sdf.data.size() * sizeof(float));
    data.close();

    return true;
}

}  // namespace cpu
