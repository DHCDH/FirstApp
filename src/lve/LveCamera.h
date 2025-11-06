#pragma once

#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>

namespace lve { 

/*将相机姿态与投影参数封装成V和P矩阵*/
class LveCamera {
public:

	void SetOrthographicProjection(
		float left, float right,
		float top, float bottom,
		float near, float far
	);

	void SetPerspectiveProjection(float fovy, float aspect, float near, float far);

	/*相机位置，相机指向方向，指示向上的方向（默认为负轴方向）*/
	void SetViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
	void SetViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up = glm::vec3{ 0.f, -1.f, 0.f });
	void SetViewYXZ(glm::vec3 position, glm::vec3 rotation);

	const glm::mat4& GetProjection() const { return m_projectionMatrix; }
	const glm::mat4& GetView() const { return m_viewMatrix; }
	const glm::mat4& GetInverseView() const { return m_inverseViewMatirx; }
	const glm::vec3 GetPosition() const { return glm::vec3(m_inverseViewMatirx[3]); }

private:
	glm::mat4 m_projectionMatrix{1.f};
	glm::mat4 m_viewMatrix{1.f};	// 视图矩阵，存储相机变换
	glm::mat4 m_inverseViewMatirx{1.f}; // 逆视图矩阵
};

}