#include "Blank.h"

namespace entity
{
lve::LveObject Blank::CreateObject()
{
    p_model = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    auto blank = lve::LveObject::CreateObject();
    blank.model = p_model;
    blank.transform.translation = {0.f, 0.f, 0.f};
    blank.transform.rotation = {0.f, 0.f, 0.f};

    m_id = blank.getId();

    /*texture*/
    VkDescriptorSet blankMatSet =
        GetRenderContext().textureManager.GetOrCreateMaterialSet(
            "D:/Data/Study/vulkan/FirstApp/res/textures/"
            "TCom_BrushedStainlessSteel_header.jpg",
            true);

    lve::MaterialUBO matBlank{};
    matBlank.baseColorFactor = {0.5f, 0.5f, 0.5f, 0.5f};
    matBlank.uvTilingOffset = {1, 1, 0, 0};
    matBlank.pbrAoAlpha = {0.0f, 0.5f, 1.0f, 0.0f};
    matBlank.flags = {0u, 0u, 0u, 0u};
    AssignMaterialParamsToObject(blank.getId(), matBlank);

    return blank;
}
}  // namespace entity