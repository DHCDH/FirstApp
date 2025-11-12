#pragma once

#include <glm.hpp>

class MotionControllerBase
{
public:
	struct Pose
	{
		glm::vec3 position{};
		glm::vec3 orientation{};
	};

private:

};