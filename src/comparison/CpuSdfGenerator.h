#pragma once

#include "CpuTypes.h"

namespace cpu
{

// --- CPU端SDF生成器 ---
// 对应GPU管线: GrindingWheelWearCalculatorNew + shader_sdf_generate.comp
// 输入：目标容屑槽点集（物理坐标）
// 输出：2D有向距离场图像 SdfImage
class CpuSdfGenerator
{
public:
    CpuSdfGenerator() = default;
    ~CpuSdfGenerator() = default;

    // 解析容屑槽点文件（兼容GPU端相同的文件格式）
    // 文件格式: 每行 (y, z) 或直接 y, z 浮点数
    bool ParseFlutePointSet(const std::string& filePath);

    // 计算SDF映射信息（点集的包围盒 → 像素网格参数），返回 (xMin, zMin, dx, dz)
    // 对应 GrindingWheelWearCalculatorNew::CalculateSdfMapInfo
    glm::vec4 CalculateSdfMapInfo(uint32_t width, uint32_t height) const;

    // 生成SDF图像（暴力搜索最小距离）
    // 对应 shader_sdf_generate.comp 的逻辑
    void Generate(uint32_t width, uint32_t height, const SdfGenConfig& config);

    // 获取结果
    const SdfImage& GetSdf() const
    {
        return m_sdf;
    }

    // 获取已解析的容屑槽点集
    const std::vector<glm::vec4>& GetFlutePoints() const
    {
        return m_flutePoints;
    }

private:
    std::vector<glm::vec4> m_flutePoints;  // (x=0, y, z, w=1) 世界坐标
    SdfImage m_sdf;
};

}  // namespace cpu
