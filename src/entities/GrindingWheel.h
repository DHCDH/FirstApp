#pragma once

#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "GrindingWheelController.h"
#include "Entity.h"

struct HelixMotion {
    bool enabled = true;
    float f = 1.f;                      // 进给速率 mm/s
    float pitch = 54.414f;              // 导程 mm
    float act = 0.f;                    // 累计时间 s
    float theta = 0.f;                  // 砂轮绕Y轴累计旋转角度 rad
    float y = 0.f;                      // 砂轮沿Y轴累计平移距离 mm
    glm::mat4 M0 = glm::mat4(1.f);      // 初始位姿矩阵
};

class GrindingWheel : public Entity
{
public:
    using Entity::Entity;

    void SetMotionEnabled(bool enabled) { m_motionEnabled = enabled; }
    bool GetMotionEnabled() const { return m_motionEnabled; }

    lve::LveObject CreateObject() override;
    lve::TransformComponent Update(const float& dt);
    lve::TransformComponent EvaluateAtTime(const float& t);

private:
    std::string m_filepath{"D:/Data/Study/vulkan/FirstApp/res/models/StillGrindingWheel.obj"};
    bool m_motionEnabled = false;
    uint32_t m_id{114514};
    HelixMotion m_helixMotion;
    glm::mat4 m_modelMatrix{1.f};
};