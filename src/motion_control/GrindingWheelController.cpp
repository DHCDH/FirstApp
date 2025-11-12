#include "GrindingWheelController.h"

GrindingWheelController::GrindingWheelController(const Pose& startPose, const double& axialFeedRate, const double& cuttingSpeed)
	: m_startPose(startPose), m_axialFeedRate(axialFeedRate), m_cuttingSpeed(cuttingSpeed)
{

}

