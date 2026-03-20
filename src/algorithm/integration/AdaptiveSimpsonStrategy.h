#pragma once

#include "NumericalIntegrator.h"

class AdaptiveSimpsonStrategy : public IntegrationStrategy
{
public:
    AdaptiveSimpsonStrategy(double atol, double rtol, double h_init, double h_min,
                            double h_max)
        : atol(atol), rtol(rtol), h_init(h_init), h_min(h_min), h_max(h_max)
    {
    }

    double Integrate(const std::function<double(double)>& f, double u0,
                     double u1) const override;

    std::string Name() const override;

private:
    double RK4StepScalar(double u, double h, const std::function<double(double)>& f) const;

    double atol;    // 绝对误差容限
    double rtol;    // 相对误差容限
    double h_init;  // 初始步长
    double h_min;   // 最小允许步长
    double h_max;   // 最大允许步长
};