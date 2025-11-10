#include "LveCamera.h"

#include <cassert>
#include <limits>
#include <iostream>

namespace lve { 

void LveCamera::SetOrthographicProjection(
	float left, float right, float top, float bottom, float near, float far)
{
	m_projectionMatrix = glm::mat4{ 1.0f };
	m_projectionMatrix[0][0] = 2.f / (right - left);
	m_projectionMatrix[1][1] = 2.f / (bottom - top);
	m_projectionMatrix[2][2] = 1.f / (far - near);
	m_projectionMatrix[3][0] = -(right + left) / (right - left);
	m_projectionMatrix[3][1] = -(bottom + top) / (bottom - top);
	m_projectionMatrix[3][2] = -near / (far - near);
}

void LveCamera::SetPerspectiveProjection(float fovy, float aspect, float near, float far) 
{
	assert(glm::abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);
	const float tanHalfFovy = tan(fovy / 2.f);
	m_projectionMatrix = glm::mat4{ 0.0f };
	m_projectionMatrix[0][0] = 1.f / (aspect * tanHalfFovy);
	m_projectionMatrix[1][1] = 1.f / (tanHalfFovy);
	m_projectionMatrix[2][2] = far / (far - near);
	m_projectionMatrix[2][3] = 1.f;
	m_projectionMatrix[3][2] = -(far * near) / (far - near);
}

/* 用相机位置和朝向构造视图矩阵
 * @param position 视图位置
 * @param direction 视图方向
 * @param up 视图up向量
 * @note direction不能为0且不能与up共线
*/
void LveCamera::SetViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up) 
{
	/*构建标准正交基u，v，w*/
	const glm::vec3 w{ glm::normalize(direction) };
	const glm::vec3 u{ glm::normalize(glm::cross(w, up)) };
	const glm::vec3 v{ glm::cross(w, u) };

	m_viewMatrix = glm::mat4{ 1.f };
	m_viewMatrix[0][0] = u.x;
	m_viewMatrix[1][0] = u.y;
	m_viewMatrix[2][0] = u.z;
	m_viewMatrix[0][1] = v.x;
	m_viewMatrix[1][1] = v.y;
	m_viewMatrix[2][1] = v.z;
	m_viewMatrix[0][2] = w.x;
	m_viewMatrix[1][2] = w.y;
	m_viewMatrix[2][2] = w.z;
	/*旋转完后，把世界坐标系平移，使得相机的位置变成原点*/
	m_viewMatrix[3][0] = -glm::dot(u, position);
	m_viewMatrix[3][1] = -glm::dot(v, position);
	m_viewMatrix[3][2] = -glm::dot(w, position);

	m_inverseViewMatirx = glm::inverse(m_viewMatrix);
}

/*设置视图目标*/
void LveCamera::SetViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up) 
{
	/*最好添加断言检查target-position是否为0*/
	assert(glm::length(target - position) > std::numeric_limits<float>::epsilon() && "invalid value: target - position");
	SetViewDirection(position, target - position, up);
}

/*用相机位置和欧拉角Y->X->Z*直接构造视图矩阵*/
void LveCamera::SetViewYXZ(glm::vec3 position, glm::vec3 rotation) 
{
	const float c3 = glm::cos(rotation.z);
	const float s3 = glm::sin(rotation.z);
	const float c2 = glm::cos(rotation.x);
	const float s2 = glm::sin(rotation.x);
	const float c1 = glm::cos(rotation.y);
	const float s1 = glm::sin(rotation.y);
	const glm::vec3 u{ (c1 * c3 + s1 * s2 * s3), (c2 * s3), (c1 * s2 * s3 - c3 * s1) };
	const glm::vec3 v{ (c3 * s1 * s2 - c1 * s3), (c2 * c3), (c1 * c3 * s2 + s1 * s3) };
	const glm::vec3 w{ (c2 * s1), (-s2), (c1 * c2) };
	m_viewMatrix = glm::mat4{ 1.f };
	m_viewMatrix[0][0] = u.x;
	m_viewMatrix[1][0] = u.y;
	m_viewMatrix[2][0] = u.z;
	m_viewMatrix[0][1] = v.x;
	m_viewMatrix[1][1] = v.y;
	m_viewMatrix[2][1] = v.z;
	m_viewMatrix[0][2] = w.x;
	m_viewMatrix[1][2] = w.y;
	m_viewMatrix[2][2] = w.z;
	m_viewMatrix[3][0] = -glm::dot(u, position);
	m_viewMatrix[3][1] = -glm::dot(v, position);
	m_viewMatrix[3][2] = -glm::dot(w, position);
}

}