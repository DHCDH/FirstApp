#include "Camera.h"

namespace entity
{
lve::LveObject Camera::CreateObject()
{
    p_model = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    auto camera = lve::LveObject::CreateObject();
    camera.model = p_model;
    camera.transform.translation = {70.f, -10.f, 0.f};
    camera.transform.rotation = {0.f, glm::pi<float>()/2.f, 0.f};
    camera.transform.scale = {2.2f, 2.2f, 2.2f};

    // 设置透明度
    camera.transparency = 1.f;

    m_id = camera.getId();

    auto subCount = camera.model->GetSubmeshCount();
    GetRenderContext().submeshTextureSets[m_id].resize(subCount);
    GetRenderContext().submeshMatSets[m_id].resize(subCount);

    m_materialUBO.baseColorFactor = {0.1f, 0.1f, 0.1f, 1.f};
    // flag第一位为4，表示camera
    // flag第四位为1，需要渲染线框
    m_materialUBO.flags = {4u, 0u, 0u, 0u};

    // 【修改】：用 for 循环给所有的 submesh 绑定材质参数
    for (uint32_t i = 0; i < subCount; ++i) {
        CreateMaterialParamSetsForSubmesh(m_id, i, m_materialUBO);
    }
    CreateMaterialParamSetsForSubmesh(m_id, 0, m_materialUBO);

    return camera;
}
}  // namespace entity