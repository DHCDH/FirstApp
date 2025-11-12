#pragma once

#include "MotionControllerBase.h"

class GrindingWheelController : public MotionControllerBase
{
public:
	GrindingWheelController(const Pose& startPose, const double& axialFeedRate, const double& cuttingSpeed);

	void SetAxialFeedRate(const double& feedRate) { m_axialFeedRate = feedRate; }
	void SetStartPose(const Pose& pose) { m_startPose = pose; }
	
	Pose GetStartPose() const { return m_startPose; }
	double GetAxialFeedRate() const { return m_axialFeedRate; }
	double GetCuttingSpeed() const { return m_cuttingSpeed; }

private:
	Pose m_startPose;		// 初始位姿
	double m_axialFeedRate;	// 轴向进给速度 mm/s
	double m_cuttingSpeed;	// 线速度 mms/s
};