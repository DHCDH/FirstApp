#include "CpuWearFitter.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "../Global.h"

namespace cpu
{

float CpuWearFitter::FitGoldenSection(const FitConfig& config, LossEvalFn evalLoss)
{
    float a = config.grMin;
    float b = config.grMax;
    constexpr float ratio = 0.382f;  // 1 - phi, phi = (sqrt(5)-1)/2 ≈ 0.618

    float x1 = a + ratio * (b - a);
    float x2 = b - ratio * (b - a);
    float loss1 = evalLoss(x1);
    float loss2 = evalLoss(x2);

    for (uint32_t iter = 0; iter < config.maxIterations; ++iter) {
        if ((b - a) < config.tolerance) {
            break;
        }

        if (loss1 < loss2) {
            b = x2;
            x2 = x1;
            loss2 = loss1;
            x1 = a + ratio * (b - a);
            loss1 = evalLoss(x1);
        } else {
            a = x1;
            x1 = x2;
            loss1 = loss2;
            x2 = b - ratio * (b - a);
            loss2 = evalLoss(x2);
        }
    }

    return (a + b) * 0.5f;
}

float CpuWearFitter::FitBrent(const FitConfig& config, BrentLossFn evalLoss, void* context)
{
    constexpr float cgold = 0.381966011f;  // 1 - phi
    const float eps = std::numeric_limits<float>::epsilon();
    const float tol = config.tolerance;

    float a = config.grMin;
    float b = config.grMax;

    // x = 当前最佳点, w = 第二佳点, v = 第三佳点
    float x = a + cgold * (b - a);
    float fx = evalLoss(x, context);
    float w = x, v = x;
    float fw = fx, fv = fx;

    float d = 0.f;      // 本轮步长
    float ePrev = 0.f;  // 上轮步长

    for (uint32_t iter = 1; iter <= config.maxIterations; ++iter) {
        float xm = (a + b) * 0.5f;
        float tol1 = eps * std::abs(x) + tol / 3.f;
        float tol2 = 2.f * tol1;

        // 收敛判断: 区间足够小
        if (std::abs(x - xm) <= tol2 - (b - a) * 0.5f) {
            break;
        }

        bool useGolden = true;
        float p = 0.f, q = 0.f, r = 0.f;

        // 尝试逆抛物线插值（用点 (x,fx), (w,fw), (v,fv) 拟合抛物线求极小点）
        if (std::abs(ePrev) > tol1) {
            r = (x - w) * (fx - fv);
            q = (x - v) * (fx - fw);
            p = (x - v) * q - (x - w) * r;
            q = 2.f * (q - r);
            if (q > 0.f) {
                p = -p;
            } else {
                q = -q;
            }
            r = ePrev;
            ePrev = d;

            // 抛物线步长必须在区间内且正在缩小
            if (std::abs(p) < std::abs(0.5f * q * r) &&
                p > q * (a - x) &&
                p < q * (b - x)) {
                d = p / q;
                float u = x + d;
                if (u - a < tol2 || b - u < tol2) {
                    d = (xm < x) ? -tol1 : tol1;
                }
                useGolden = false;
            }
        }

        // 黄金分割回退
        if (useGolden) {
            ePrev = (x < xm) ? b - x : a - x;
            d = cgold * ePrev;
        }

        // 步长至少为 tol1
        float u;
        if (std::abs(d) >= tol1) {
            u = x + d;
        } else {
            u = x + (d > 0.f ? tol1 : -tol1);
        }

        float fu = evalLoss(u, context);

        // 更新 a, b, x, w, v
        if (fu <= fx) {
            if (u < x) b = x;
            else       a = x;
            v = w;  fv = fw;
            w = x;  fw = fx;
            x = u;  fx = fu;
        } else {
            if (u < x) a = u;
            else       b = u;
            if (fu <= fw || w == x) {
                v = w;  fv = fw;
                w = u;  fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u;  fv = fu;
            }
        }
    }

    return x;
}

float CpuWearFitter::FitGoldenSectionBatch(const FitConfig& config,
                                            LossPairEvalFn evalLossPair)
{
    float a = config.grMin;
    float b = config.grMax;
    constexpr float ratio = 0.382f;

    float x1 = a + ratio * (b - a);
    float x2 = b - ratio * (b - a);

    for (uint32_t iter = 0; iter < config.maxIterations; ++iter) {
        if ((b - a) < config.tolerance) {
            break;
        }

        LossResult result = evalLossPair(x1, x2);

        if (result.loss1 < result.loss2) {
            b = x2;
            x2 = x1;
            x1 = a + ratio * (b - a);
        } else {
            a = x1;
            x1 = x2;
            x2 = b - ratio * (b - a);
        }
    }

    return (a + b) * 0.5f;
}

}  // namespace cpu
