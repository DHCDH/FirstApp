#include "AdaptiveSimpsonStrategy.h"

#include <algorithm>

double AdaptiveSimpsonStrategy::Integrate(const std::function<double(double)>& f, double u0,
                                      double u1) const
{
    const double safety{0.9};
    const double minFactor{0.2};
    const double maxFactor{5.0};
    const int order{4};
    const double pow_inv{1.0 / (order + 1)};

    double sign = (u1 >= u0) ? 1.0 : -1.0;
    double u{u0};
    double z{0.};
    double h = std::clamp(h_init, h_min, h_max) * sign;

    while (sign * (u - u1) < 0.0) {
        // limit step to not overshoot
        if (std::abs(h) > std::abs(u1 - u)) {
            h = (u1 - u);
        }

        // one full step
        double increment_full;
        try {
            increment_full = RK4StepScalar(u, h, f);
        } catch (...) {
            throw;  // propagate
        }

        // two half steps
        double half = 0.5 * h;
        double inc1 = RK4StepScalar(u, half, f);
        double inc2 = RK4StepScalar(u + half, half, f);
        double increment_half = inc1 + inc2;

        // error estimate for RK4: (increment_half - increment_full)/15
        double err_est = std::abs(increment_half - increment_full) / 15.0;

        // tolerance for this step (absolute + relative)
        double tol_step =
            atol + rtol * std::max(std::abs(increment_half), std::abs(increment_full));

        if (err_est <= tol_step) {
            // accept step
            z += increment_half;
            u += h;
            // adapt step size
            double factor = safety * std::pow(tol_step / (err_est + 1e-300), pow_inv);
            factor = std::clamp(factor, minFactor, maxFactor);
            h *= factor;
            // clamp within limits
            if (std::abs(h) < h_min) h = (h_min * sign);
            if (std::abs(h) > h_max) h = (h_max * sign);
        } else {
            // reject step, decrease h
            double factor = safety * std::pow(tol_step / (err_est + 1e-300), pow_inv);
            factor = std::clamp(factor, minFactor, maxFactor);
            h *= factor;
            if (std::abs(h) < h_min) {
                throw std::runtime_error(
                    "Step size underflow: required step < h_min. Integration failed near "
                    "u = " +
                    std::to_string(u));
            }
            // retry with new h
        }
    }

    return z;
}

double AdaptiveSimpsonStrategy::RK4StepScalar(double u, double h,
                                          const std::function<double(double)>& f) const
{
    double k1 = f(u);

    double k2 = f(u + 0.5 * h);
    double k3 = f(u + 0.5 * h);
    double k4 = f(u + h);
    return h * (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;
}

std::string AdaptiveSimpsonStrategy::Name() const
{
    return "Adaptive Simpson";
}