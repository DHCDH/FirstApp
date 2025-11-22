#include "Blank.h"

lve::LveObject Blank::CreateObject()
{
	std::shared_ptr<lve::LveModel> lveModel = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
	auto blank = lve::LveObject::CreateObject();
	blank.model = lveModel;
	blank.transform.translation = { 0.f, 0.f, 0.f};
	blank.transform.rotation = { 0.f, 0.f, 0.f };

	m_id = blank.getId();

	/*texture*/
	VkDescriptorSet blankMatSet =
		GetRenderContext().textureManager.GetOrCreateMaterialSet(
			"D:/Data/Study/vulkan/FirstApp/res/textures/TCom_BrushedStainlessSteel_header.jpg",
			true);

	lve::MaterialUBO matBlank{};
	matBlank.baseColorFactor = { 1,1,1,1 };
	matBlank.uvTilingOffset = { 1,1,0,0 };
	matBlank.pbrAoAlpha = { 0.0f, 0.5f, 1.0f, 0.0f };
	matBlank.flags = { 0u, 0u, 0u, 0u };
	AssignMaterialParamsToObject(blank.getId(), matBlank);

	return blank;
}