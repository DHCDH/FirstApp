#pragma once

#include <stdexcept>
#include <format>

class CalculateGrindingWheelMaxRadius
{
public:
    /**
     * @brief 计算1V1砂轮倒圆角后的最大半径
     * @param R 砂轮半径（倒圆角前）
     * @param gr 圆角半径
     * @param ga 砂轮角度（deg）
     * @return 砂轮倒圆角后的最大半径
     */
    static float Calculate1V1GrindingWheelMaxRadius(float R, float gr, float ga)
    {
        const float PI = 3.14159265;
        if (ga < 0.f || ga >= PI / 2) {
            throw std::runtime_error(std::format("Invalid angle: {}", ga));
        }

        return R + gr * (1 - 1 / tan(ga / 2));
    }
};