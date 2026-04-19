#pragma once

#include "Entity.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "MotionControllerBase.h"

namespace entity
{
class Blank : public Entity
{
public:
    using Entity::Entity;

    lve::LveObject CreateObject() override;

private:
    std::string m_filepath{
        "D:\\Data\\Study\\vulkan\\FirstApp\\res\\models\\blanks\\flat\\blank_flat_D10_"
        "L40_X\\blank_flat_D10_L40_X.obj"};
    // std::string m_filepath{
    //    "D:/Data/Study/vulkan/FirstApp/output_stuff/img/model_for_img/plane.obj"};
    uint32_t m_id{};
};
}  // namespace entity