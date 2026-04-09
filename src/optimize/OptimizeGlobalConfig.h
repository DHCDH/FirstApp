#pragma once

#include <functional>
#include <glm.hpp>

namespace optimize
{
struct GrindingWheelParameters {
    double gR{50.};  // 大端圆半径
    double gr1{0.1};  // 大端面圆角
    double gr2{0.2};  // 小端面圆角
    double width{10.};
    
    std::function<double(double)> radius{[this](double) { return 50.; }};
    std::function<double(double)> radiusDeriv{[this](double) { return 0.; }};
};

struct CutterParameters {
    double cuttingEdgeLength{30.};  // 切削刃长度

    // 默认为不变螺旋角
    std::function<double(double)> helixAngle{[](double u1) { return 30.; }};

    // 默认为圆柱形铣刀
    std::function<double(double)> radius{[](double u1) { return 5.; }};  // 外轮廓半径
    std::function<double(double)> radiusDeriv{[](double u1) { return 0.; }};
    std::function<double(double)> coreRadius{[](double u1) { return 3.; }};
    std::function<double(double)> slotAngle{[](double u1) { return 65.; }};

    std::function<double(double)> radialRakeAngle{
        [](double u1) { return 10.; }};  // 径向前角
};

}  // namespace optimize