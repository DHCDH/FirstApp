#pragma once

#include <string>
#include <vector>

#include <glm.hpp>

namespace cpu
{

struct FluteMeasureResult
{
    float coreRadius = 0.f;  // 芯厚半径 (mm)
    float slotAngle = 0.f;   // 槽宽角 (deg)
    float rakeAngle = 0.f;   // 前角 (deg)
};

// 模仿 res/shaders/calculate_rake_angle/shader_rake_angle.comp 的算法，
// 从已排序的容屑槽截面 2D 点集（原点为刀具轴线）计算芯厚半径、槽宽角和前角。
class FluteMeasure
{
public:
    // 从 txt 文件加载点集，每行 "x, y"
    static bool LoadPoints(const std::string& path, std::vector<glm::vec2>& points);

    static FluteMeasureResult Measure(const std::vector<glm::vec2>& points);
};

}  // namespace cpu
