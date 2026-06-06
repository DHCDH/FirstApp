#pragma once

#include <functional>

#include "CpuTypes.h"

namespace cpu
{

// --- CPU端砂轮圆角半径优化器 ---
// 对应GPU管线: SliceWearFitter
//
// 使用 Brent 方法（混合逆抛物线插值与黄金分割回退）搜索最优圆角半径
// 目标: argmin_{gr} Loss(gr)  —— 其中Loss为交集边缘与目标SDF之间的MSE
//
// Brent方法比纯黄金分割快2-3倍，在光滑单峰函数上通常5-8次迭代即可收敛
class CpuWearFitter
{
public:
    CpuWearFitter() = default;
    ~CpuWearFitter() = default;

    // Loss评估函数签名: float(float candidateRadius)
    using LossEvalFn = std::function<float(float)>;

    // --- 黄金分割搜索 (Golden Section Search) ---
    // 使用固定的黄金比例 0.382 缩小区间
    // 每轮评估两个候选点，保留较优一侧
    float FitGoldenSection(const FitConfig& config, LossEvalFn evalLoss);

    // --- Brent 方法 ---
    // 结合逆抛物线插值与黄金分割回退
    // 在光滑单峰函数上收敛更快
    float FitBrent(const FitConfig& config, LossEvalFn evalLoss);

    // --- 用于批量评估两个候选点（多切片平面聚合）---
    // 返回两个候选半径的聚合Loss
    using LossPairEvalFn = std::function<LossResult(float x1, float x2)>;
    float FitGoldenSectionBatch(const FitConfig& config, LossPairEvalFn evalLossPair);
};

}  // namespace cpu
