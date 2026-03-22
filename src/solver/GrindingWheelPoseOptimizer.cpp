#include "GrindingWheelPoseOptimizer.h"

#include "ArcProjectionSolver.h"

GrindingWheelPoseOptimizer::GrindingWheelPoseOptimizer()
{
}

void GrindingWheelPoseOptimizer::CalculateTransformMatrixes()
{
    auto integrator = std::make_unique<NumericalIntegrator>();
    integrator->SetStrategy(
        std::make_unique<AdaptiveSimpsonStrategy>(1e-10, 1e-10, 1e-2, 1e-12, 0.5));
    ArcProjectionSolver arcProjectionSolver;
    arcProjectionSolver.SetIntegrator(std::move(integrator))
        .SetCutterParameters(CutterParameters{})
        .SetGrindingWheelParameters(GrindingWheelParameters{});

    m_transformMatrixes = arcProjectionSolver.CalculateGrindingWheelPose();


}

GrindingWheelPoseOptimizer::~GrindingWheelPoseOptimizer()
{
}