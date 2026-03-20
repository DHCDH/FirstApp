#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

class IntegrationStrategy
{
public:
    virtual ~IntegrationStrategy() = default;
    virtual double Integrate(const std::function<double(double)>& f, double a,
                             double b) const = 0;
    virtual std::string Name() const = 0;
};

class NumericalIntegrator
{
public:
    NumericalIntegrator() = default;
    ~NumericalIntegrator() = default;

    void SetStrategy(std::unique_ptr<IntegrationStrategy> strategy)
    {
        strategy_ = std::move(strategy);
    }

    double Integrate(const std::function<double(double)>& f, double u0, double u1) const
    {
        if (!strategy_) {
            throw std::runtime_error("No integration strategy set!");
        }
        return strategy_->Integrate(f, u0, u1);
    }

    std::string getStrategyName() const
    {
        if (!strategy_) {
            return "No strategy set!";
        }
        return strategy_->Name();
    }

private:
    std::unique_ptr<IntegrationStrategy> strategy_;
};