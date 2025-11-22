#include "GrindingWheel.h"

#include "LveModel.h"

#include <gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/quaternion.hpp>

lve::LveObject GrindingWheel::CreateObject()
{
	std::shared_ptr<lve::LveModel> lveModel = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
	auto grindingWheel = lve::LveObject::CreateObject();
	grindingWheel.model = lveModel;
	// grindingWheel.transform.translation = { -45.9003f, 13.0961f , 16.0312f };
	// grindingWheel.transform.rotation = {-0.949079, 0.014354, 0.027942 };
	grindingWheel.transform.translation = { 0.f, 0.f, 0.f };
	grindingWheel.transform.rotation = {0.f, 0.f, 0.f};

	m_id = grindingWheel.getId();

	/*texture*/
	auto subCount = grindingWheel.model->GetSubmeshCount();
	GetRenderContext().submeshTextureSets[m_id].resize(subCount);
	GetRenderContext().submeshMatSets[m_id].resize(subCount);
	VkDescriptorSet dsAbrasive = GetRenderContext().textureManager.GetOrCreateMaterialSet(
		"D:/Data/Study/vulkan/FirstApp/res/textures/TCom_OldAluminium_header.jpg", true);
	VkDescriptorSet dsMetal = GetRenderContext().textureManager.GetOrCreateMaterialSet(
		"D:/Data/Study/vulkan/FirstApp/res/textures/Grinding_Wheel.jpg", true);
	GetRenderContext().submeshTextureSets[m_id][0] = dsAbrasive;
	GetRenderContext().submeshTextureSets[m_id][1] = dsMetal;

	lve::MaterialUBO mtlAbr{};
	mtlAbr.baseColorFactor = { 1,1,1,1 };
	mtlAbr.uvTilingOffset = { 1,1,0,0 };
	mtlAbr.pbrAoAlpha = { 0.0f, 0.85f, 1.0f, 0.0f }; // 非金属、粗糙高
	mtlAbr.flags = { 1u,0u,0u,0u };

	lve::MaterialUBO mtlMetal{};
	mtlMetal.baseColorFactor = { 1,1,1,1 };
	mtlMetal.uvTilingOffset = { 1,1,0,0 };
	mtlMetal.pbrAoAlpha = { 1.0f, 0.35f, 1.0f, 0.0f }; // 金属、粗糙低
	mtlMetal.flags = { 1u,0u,0u,0u };

	// 为两个 submesh 生成“每帧一套”的 set=2
	CreateMaterialParamSetsForSubmesh(m_id, 0, mtlAbr);
	CreateMaterialParamSetsForSubmesh(m_id, 1, mtlMetal);

	m_helixMotion.M0 = m_modelMatrix;

	return grindingWheel;
}

lve::TransformComponent GrindingWheel::Update(const float& dt)
{
	m_helixMotion.act += dt;
	lve::TransformComponent transform{};

	constexpr float pi = glm::pi<float>();
	float omega = 2 * pi * m_helixMotion.f / m_helixMotion.pitch;
	m_helixMotion.theta += omega * dt;
	m_helixMotion.y += m_helixMotion.f * dt;

	if (m_helixMotion.y > 100.f) {
		return transform;
	}

	transform.rotation = glm::vec3(0.f, m_helixMotion.theta, 0.f);
	transform.translation = glm::vec3(0.f, m_helixMotion.y, 0.f);
	transform.scale = glm::vec3(1.f);

	return transform;
}

lve::TransformComponent GrindingWheel::EvaluateAtTime(const float& t)
{
	constexpr float pi = glm::pi<float>();
	float pitch = m_helixMotion.pitch;
	float feedRate = m_helixMotion.f;
	float omega = 2 * pi * feedRate / pitch;

	m_helixMotion.theta = omega * t;
	m_helixMotion.y = feedRate * t;

	lve::TransformComponent transform{};
	transform.rotation = glm::vec3(0.f, m_helixMotion.theta, 0.f);
	transform.translation = glm::vec3(0.f, m_helixMotion.y, 0.f);
	transform.scale = glm::vec3(1.f);

	return transform;
}