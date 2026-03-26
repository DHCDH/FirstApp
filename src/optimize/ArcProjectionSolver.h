#pragma once

#include <functional>
#include <glm.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "LveDevice.h"
#include "integration/NumericalIntegrator.h"
#include "../Global.h"

struct GrindingWheelParameters {
    double d1{100.};  // 大端圆直径
    double d2{100.};  // 小端圆直径
    double width{10.};
    double gr1{0.1};  // 大端面圆角
    double gr2{0.2};  // 小端面圆角
    // 默认为1A1砂轮
    std::function<double(double)> radius{[&](double u0) {
        // 磨削前角仅用到大端面圆角
        if (u0 < gr1) {
            return (d1 / 2) - gr1 + std::sqrt(gr1 * gr1 - (gr1 - u0) * (gr1 - u0));
        } else if (u0 > (width - gr1) && u0 <= width) {
            return (d1 / 2) - gr2 +
                   std::sqrt(gr2 * gr2 - (u0 - (width - gr2)) * (u0 - (width - gr2)));
        }
        return 50.;
    }};
    std::function<double(double)> radiusDeriv{[&](double u0) {
        if (u0 < gr1) {
            return (gr1 - u0) / std::sqrt(gr1 * gr1 - (gr1 - u0) * (gr1 - u0));
        } else if (u0 > (width - gr1) && u0 <= width) {
            return (width - gr2 - u0) /
                   std::sqrt(gr2 * gr2 - (u0 - width + gr2) * (u0 - width + gr2));
        }
        return 0.;
    }};
};

struct CutterParameters {
    double cuttingEdgeLength{30.};  // 切削刃长度

    // 默认为不变螺旋角
    std::function<double(double)> helixAngle{[](double u1) { return glm::radians(30.); }};

    // 默认为圆柱形铣刀
    std::function<double(double)> radius{[](double u1) { return 5.; }};  // 外轮廓半径
    std::function<double(double)> radiusDeriv{[](double u1) { return 0.; }};
    std::function<double(double)> coreRadius{[](double u1) { return 3.; }};

    std::function<double(double)> radialRakeAngle{
        [](double u1) { return glm::radians(10.); }};  // 径向前角
};

// 参考了圆弧投影的论文，并非使用圆弧投影法
class ArcProjectionSolver
{
public:
    ArcProjectionSolver();
    ~ArcProjectionSolver();

    ArcProjectionSolver(const ArcProjectionSolver&) = delete;
    ArcProjectionSolver& operator=(const ArcProjectionSolver&) = delete;

    std::vector<glm::mat4> CalculateGrindingWheelPose();

    void ExportTransformsToTXT();
    void ExportToolPathToTXT();

public:
    ArcProjectionSolver& SetIntegrator(std::unique_ptr<NumericalIntegrator> integrator)
    {
        m_integrator = std::move(integrator);
        return *this;
    }
    ArcProjectionSolver& SetGrindingWheelParameters(GrindingWheelParameters gw)
    {
        m_gw = std::move(gw);
        return *this;
    }
    ArcProjectionSolver& SetCutterParameters(CutterParameters c)
    {
        m_c = std::move(c);
        return *this;
    }

private:
    // --- 论文公式计算 ---
    // Eq.19 计算符合前角和螺旋角的砂轮位置
    void NarrowGrindingWheelPosition(double u0c, double lambda, double u1);

    // Eq.2，通过积分求解θ(u1)
    double CalculateTheta(double u1);
    // Eq.3，计算切削刃曲线
    glm::dvec4 CalculateCuttingEdgeCurve(double u1, double theta1);
    // Eq.4 切削刃曲线单位切向量
    glm::dvec4 CalculateCuttingEdgeCurveTangent(double u1, double theta1);
    // Eq.5 & Eq.6 刀坯回转面单位法向量bt
    glm::dvec4 CalculateRevolutionSurfaceNormal(double u1, double theta1);
    // Eq.7 m_t
    glm::dvec4 CalculateMt(const glm::dvec4& b_t, const glm::dvec4& t_t);
    // Eq.8 切削刃曲线单位法向量nt_1
    glm::dvec4 CalculateCuttingEdgeCurveNormal(double u1, double theta1,
                                               const glm::dvec4& r_t1);
    // Eq.9 计算法向前角
    double CalculateNormalRakeAngle(double u1, const glm::dvec4& r_t1,
                                    const glm::dvec4& b_t, const glm::dvec4& m_t);

    std::unique_ptr<NumericalIntegrator> m_integrator;

    GrindingWheelParameters m_gw{};
    CutterParameters m_c{};
    // std::vector<double> m_u1;
    // std::vector<double> m_u0c;
    // std::vector<double> m_lambda;
    std::vector<glm::mat4> m_transform;
    ToolPath m_tp;
};
