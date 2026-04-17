#pragma once

#include "Entity.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "MotionControllerBase.h"

namespace entity
{
class Camera : public Entity
{
public:
    using Entity::Entity;

    lve::LveObject CreateObject() override;

private:
    std::string m_filepath{
        "D:/Data/Study/vulkan/FirstApp/output_stuff/img/model_for_img/camera/nano_camera/Runcam Nano.obj"};
    uint32_t m_id{};
};
}  // namespace entity