#pragma once

#include <functional>

#include "../Global.h"
#include "CpuTypes.h"

namespace cpu
{
class CalculateFlute
{
public:
    CalculateFlute(const WheelParams& gp, const std::vector<ToolPath>& tp)
        : m_gp(gp), m_toolpaths(tp){};
    ~CalculateFlute(){};

    CalculateFlute(const CalculateFlute&) = delete;
    CalculateFlute& operator=(const CalculateFlute&) = delete;

    std::vector<glm::vec3> CreateFlute();

    // 将容屑槽点集导出为文件，格式: y, z (每行两个float，逗号分隔)
    static void ExportFlutePoints(const std::vector<glm::vec3>& points);

private:
    void InitGrindingWheelEquation();
    glm::mat4 CalculateGrindingWheelPose();
    glm::mat4 CalculateMatrix(const glm::vec3& normal, const glm::vec3& point);
    std::vector<glm::vec3> CalculateFlutePointSet(const glm::mat4& M);
    glm::vec3 SolveNewton2D(float m1, float t, float z, float fg, const glm::mat4& M);
    std::vector<glm::vec3> ProcessPointSet(std::vector<glm::vec3> pSet);

private:
    GrindingWheelType m_grndWheelType = GrindingWheelType::PERIPHERY;
    std::vector<ToolPath> m_toolpaths;

    WheelParams m_gp;
    std::vector<std::function<float(float)>> m_gEquations;
    std::vector<std::pair<float, float>> m_gRanges;
};

}  // namespace cpu