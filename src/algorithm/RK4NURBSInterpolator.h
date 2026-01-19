#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <gtc/constants.hpp>
#include <vector>

struct ControlPoint {
    glm::dvec3 p;
    double w;  // 权重

    // 加权控制点(wx, wy, wz, w)
    glm::dvec4 ToHomogeneous() const
    {
        return glm::dvec4(p * w, w);
    }
};

class RK4NURBSInterpolator
{
public:
    RK4NURBSInterpolator();
    ~RK4NURBSInterpolator() = default;

    // 设置曲线数据
    void SetCurveData(int degree, const std::vector<double>& knots,
                      const std::vector<ControlPoint>& ctrlPts);
    // 设置动力学约束
    void SetKinematicLimits(double maxVel, double maxAcc, double maxJerk,
                            double chordError);
    // 设置插补周期
    void SetInterpolationPeriod(double T);
    //扫描曲线
    void PreProcessLookAhead();
    // 实时插补
    // 输出：当前周期的插补点位置
    // 返回： 是否达到终点，false表示插补结束
    bool GetNextInterpolationPoint(glm::dvec3& outPts);
    // 获取当前参数
    double GetCurrentU() const
    {
        return m_curU;
    }

private:
    // 速度规划节点
    struct SpeedNode {
        double u;        // 曲线参数
        double v_limit;  // 几何限速
        double v_plan;   // 最终规划速度
    };

    void Evaluate(double u, glm::dvec3& P, glm::dvec3& D1, glm::dvec3& D2) const;

    void BasisFunsDerivatives(int span, double u, std::vector<double>& N,
                              std::vector<double>& dN, std::vector<double>& ddN) const;
    int FindSpan(double u) const;

    double ComputeNextU_RK4(double curU, double curV) const;

    double CorrectStep(double curU, double nxtSetU, double curV) const;

    double GetSpeedAtU(double u) const;

    double GetArcLengthEstimate(double u1, double u2) const;
};