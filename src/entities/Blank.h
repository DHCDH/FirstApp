#pragma once

#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "MotionControllerBase.h"
#include "Entity.h"


class Blank : public Entity
{
public:
    using Entity::Entity;

    lve::LveObject CreateObject() override;

private:
    std::string m_filepath{ "D:/Data/Study/vulkan/FirstApp/res/models/blank.obj" };
    uint32_t m_id{ 114514 };
};