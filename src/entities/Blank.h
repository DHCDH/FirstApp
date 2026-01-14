#pragma once

#include "Entity.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "MotionControllerBase.h"

class Blank : public Entity
{
public:
    using Entity::Entity;

    lve::LveObject CreateObject() override;

private:
    std::string m_filepath{
        "D:/Data/Study/vulkan/FirstApp/res/models/blanks/blank_flat_D10_L70.obj"};
    uint32_t m_id{114514};
};