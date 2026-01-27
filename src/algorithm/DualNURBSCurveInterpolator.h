#pragma once

#include "../Global.h"

class DualNURBSCurveInterpolator
{
public:
    /**
     * @brief 插值函数：输入稀疏刀轨，输出密集刀轨
     * @param input 原始刀轨
     * @param stepLength 插值步长
     * @return 插值后的密集刀轨
     */
    static ToolPath Interpolate(const ToolPath& input, float stepLength);

private:
    // 计算共享参数（弦长法）
    static std::vector<double> CalculateSharedParams(const ToolPath& path);

    // 生成节点向量
    static std::vector<double> GenerateKnots(const std::vector<double>& params,
                                                int degree);

    // 反求控制点
    static std::vector<glm::dvec3> SolveControlPoints(
        const std::vector<glm::dvec3>& dataPoints, const std::vector<double>& params,
        const std::vector<double>& knots, int degree);
};
