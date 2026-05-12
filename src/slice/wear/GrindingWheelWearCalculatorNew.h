#pragma once

#include <string>
#include <vector>
#include <glm.hpp>

namespace wear
{

class GrindingWheelWearCalculatorNew
{ 
public:
    GrindingWheelWearCalculatorNew(std::string fluteFilePath);
    ~GrindingWheelWearCalculatorNew(){};

    GrindingWheelWearCalculatorNew(const GrindingWheelWearCalculatorNew&) = delete;
    GrindingWheelWearCalculatorNew& operator=(const GrindingWheelWearCalculatorNew&) = delete;

    bool ParseFlutePointSet(const std::string& path);

    glm::vec4 CalculateSdfMapInfo(uint32_t width, uint32_t height);

    std::vector<glm::vec4> GetFlutePointSet() const
    {
        return m_targetFlutePoints;
    }

private:
    std::vector<glm::vec4> m_targetFlutePoints;
};

}