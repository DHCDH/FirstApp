#pragma once

#include "Entity.h"
#include "GrindingWheelController.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"

namespace entity
{
struct HelixMotion {
    bool enabled = true;
    float f = 1.f;                  // 进给速率 mm/s
    float pitch = 54.414f;          // 导程 mm
    float act = 0.f;                // 累计时间 s
    float theta = 0.f;              // 砂轮绕Y轴累计旋转角度 rad
    float y = 0.f;                  // 砂轮沿Y轴累计平移距离 mm
    glm::mat4 M0 = glm::mat4(1.f);  // 初始位姿矩阵
};

class GrindingWheel : public Entity
{
public:
    using Entity::Entity;

    void SetMotionEnabled(bool enabled)
    {
        m_motionEnabled = enabled;
    }
    bool GetMotionEnabled() const
    {
        return m_motionEnabled;
    }

    lve::LveObject CreateObject() override;
    lve::TransformComponent Update(const float& dt);
    lve::TransformComponent EvaluateAtTime(const float& t);
    void CalculateGrindingWheelInstances(std::vector<glm::mat4>& instances,
                                         const std::vector<ToolPath>& toolPaths) const;
    void CalculateGrindingWheelInstances(std::vector<glm::mat4>& instances,
                                         const glm::mat4& baseMat) const;

private:
    // --- 计算管线用砂轮 ---
     //std::string m_filepath{
     //   "D:\\Data\\Study\\vulkan\\FirstApp\\res\\models\\grindingwheels\\1A1\\1a1_d100_"
     //   "w10_r0.1_x_calculate\\1A1_D100_W10_r0.1_x_calculate_half_less_accurate.obj"};

    // --- 模拟磨损用砂轮 ---
    std::string m_filepath{
        "D:/Data/Study/vulkan/FirstApp/res/models/grindingwheels/1V1/"
        "1V1_D100_W10_R0.1_A60/1V1_D100_W10_R0.1_A60.obj"};

    // --- 渲染用砂轮 ---
    // std::string m_filepath{
    //     "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\img\\model_for_img\\1V1\\1V1_1.obj"};

    bool m_motionEnabled = false;
    uint32_t m_id{};
    HelixMotion m_helixMotion;
    HelixMotion m_helixMotionInstanced;
    glm::mat4 m_modelMatrix{1.f};
};
}  // namespace entity