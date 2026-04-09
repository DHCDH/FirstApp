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
        "D:\\Data\\Study\\vulkan\\FirstApp\\res\\models\\blanks\\flat\\blank_flat_D12_L40_X\\blank_flat_D12_L40_X.obj"};
    //std::string m_filepath{
    //    "D:/Data/Study/vulkan/FirstApp/res/models/blanks/flat/blank_flat_D10_L50_Z/"
    //    "blank_flat_D10_L50_Z.obj"};
    uint32_t m_id{114514};
};