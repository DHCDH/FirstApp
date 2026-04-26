#pragma once

#include <glm.hpp>
#include <vector>
#include <string>
#include <spline.h>

namespace wear
{

struct CutterParameters {
    float R;
    float helixAngle;   // deg
    float lead; // 导程
};

struct WearParameters {
    glm::vec3 position; // a0, b0, c0
    glm::vec3 normal;   // d0, e0, f0
};

struct SurfacePoint {
    glm::vec3 position;
    glm::vec3 normal;
};

class GrindingWheelWearCalculator
{
public:
    GrindingWheelWearCalculator(const CutterParameters& cp, const WearParameters& wp);
    ~GrindingWheelWearCalculator();

    GrindingWheelWearCalculator(const GrindingWheelWearCalculator&) = delete;
    GrindingWheelWearCalculator& operator=(const GrindingWheelWearCalculator&) = delete;

private:
    bool ParseFlutePointSet(const std::string& path);

    // --- 拟合容屑槽函数 ---
    void FitFluteProfile();
    float GetProfileX(float t) const;
    float GetProfileY(float t) const;
    float GetProfileDx(float t) const;
    float GetProfileDy(float t) const;

    // --- 计算砂轮轮廓 ---
    void CalaulateWheelWearProfile();

    void ExportFittedCurveToTXT(const std::string& filename) const;
    void ExportVec3ToTXT(const std::vector<glm::vec3>& vec, const std::string& filename) const;
    void ExportWheelProfile(const std::vector<glm::vec2>& profile,
                            const std::string& filename);

private:
    std::vector<glm::vec3> m_flutePointSet;
    CutterParameters m_cp;
    WearParameters m_wp;
    tk::spline m_splineX;   // 拟合X坐标
    tk::spline m_splineY;   // 拟合Y坐标
    float m_maxT;
    bool m_isFitted = false;

    std::vector<glm::vec3> m_helixPoints;   // 测试用
};

}