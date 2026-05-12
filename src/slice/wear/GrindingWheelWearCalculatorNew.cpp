#include "GrindingWheelWearCalculatorNew.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "Logger.h"
#include "../Global.h"

static constexpr float MAX_FLOAT = std::numeric_limits<float>::max();

namespace wear
{
GrindingWheelWearCalculatorNew::GrindingWheelWearCalculatorNew(std::string fluteFilePath)
{

}

bool GrindingWheelWearCalculatorNew::ParseFlutePointSet(const std::string& filePath)
{
    PROFILE_SCOPE("ParseFlutePointSet");

    m_targetFlutePoints.clear();

    std::ifstream file(filePath);

    // 文件打开失败检查
    if (!file.is_open()) {
        ERROR("Failed to open file: %s", filePath.c_str());
        return false;
    }

    std::string line;
    // 逐行读取
    while (std::getline(file, line)) {
        // 跳过空行
        if (line.empty()) continue;

        // 跳过注释行、new seg、Plane 1等无效行
        if (line.front() == '#' || line.find("new seg") != std::string::npos ||
            line.find("Plane") != std::string::npos) {
            continue;
        }

        // 将逗号替换为空格，方便流读取
        for (char& ch : line) {
            if (ch == ',') ch = ' ';
        }

        // 提取x, y浮点数
        std::stringstream ss(line);
        float x, y;
        if (ss >> x >> y) {
            m_targetFlutePoints.emplace_back(0, y, x, 1.f);
        }
    }

    file.close();

    INFO("Parse flute points file succeed: total points size: %d",
         m_targetFlutePoints.size());

    return true;
}

glm::vec4 GrindingWheelWearCalculatorNew::CalculateSdfMapInfo(uint32_t width,
                                                              uint32_t height)
{
    PROFILE_SCOPE("CalculateSdfMapInfo");

    if (m_targetFlutePoints.empty()) {
        throw std::runtime_error("No target flute points found");
    }

    float minY = MAX_FLOAT, maxY = -MAX_FLOAT;
    float minZ = MAX_FLOAT, maxZ = -MAX_FLOAT;

    for (const auto& pt : m_targetFlutePoints) {
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

    DEBUG("[SDF Mapping] Geometric Center: (%f, %f)", centerY, centerZ);
    DEBUG("[SDF Mapping] Pixel accuracy dx: %fmm, dz: %fmm", dx, dz);

    return glm::vec4(centerZ - maxSide * 0.5f, centerY - maxSide * 0.5f, dx, dz);
}

}  // namespace wear