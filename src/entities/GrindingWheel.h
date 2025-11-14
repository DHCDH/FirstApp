#pragma once

#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "GrindingWheelController.h"

#include <glm.hpp>
#include <string>

class GrindingWheel 
{
public:
    GrindingWheel(lve::LveDevice& device, lve::LveModel& model);

    lve::LveObject CreateObject();
    void Update();

private:
    GrindingWheelController m_controller{ {}, 0., 0. };
    lve::LveModel& m_lveModel;
    lve::LveDevice& m_lveDevice;
    std::string m_filepath{"res/models/new_grinding_wheel.obj"};
    uint32_t m_id{114514};
};