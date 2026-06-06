#pragma once

#include "CpuTypes.h"

namespace cpu
{

// --- CPU端SDF-Loss计算器 ---
//
// 直接从解析轮廓点集计算Loss：
//   对每个轮廓点，映射到SDF坐标，采样距离值，累积MSE
//   loss = Σ clamp(dist, 3.0)² × 100000
class CpuLossCalculator
{
public:
    CpuLossCalculator() = default;
    ~CpuLossCalculator() = default;

    // points: 每个元素为 (physY, physZ)，SDF所在YZ平面上的2D物理坐标
    //   —— 来自 CalculateFlute::CreateFlute 的 vec3 需提取 (y, z) 分量传入
    float ComputeLoss(const std::vector<glm::vec2>& points, const SdfImage& sdf) const;

private:
    static constexpr float kMaxDistClamp = 3.f;
    static constexpr float kLossScale = 100000.f;
};

}  // namespace cpu
