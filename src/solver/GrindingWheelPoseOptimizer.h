#pragma once

#include <vector>
#include <glm.hpp>

#include "LveModel.h"

#include "integration/NumericalIntegrator.h"
#include "integration/AdaptiveSimpsonStrategy.h"

class GrindingWheelPoseOptimizer {
public:
    GrindingWheelPoseOptimizer();
    ~GrindingWheelPoseOptimizer();

    GrindingWheelPoseOptimizer(const GrindingWheelPoseOptimizer&) = delete;
    GrindingWheelPoseOptimizer& operator=(const GrindingWheelPoseOptimizer&) = delete;

private:
    void CalculateTransformMatrixes();

private:
    std::vector<glm::mat4> m_transformMatrixes;
    
    lve::LveModel* m_blank = nullptr;
    lve::LveModel* m_grndWheel = nullptr;

};