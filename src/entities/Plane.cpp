#include "Plane.h"

namespace entity
{
lve::LveObject Plane::CreateObject()
{
    p_model = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    auto plane = lve::LveObject::CreateObject();
    plane.model = p_model;
    plane.transform.translation = {0.f, 0.f, 0.f};
    plane.transform.rotation = {0.f, 0.f, 0.f};
    plane.transform.scale = {.5f, .5f, .5f};
    plane.transparency = 0.2f;

    m_id = plane.getId();

    auto subCount = plane.model->GetSubmeshCount();
    GetRenderContext().submeshTextureSets[m_id].resize(subCount);
    GetRenderContext().submeshMatSets[m_id].resize(subCount);

    m_materialUBO.baseColorFactor = {0.f, 0.45f, 0.85f, .4f};
    // flag第一位为1，表示plane
    // flag第二位为1，使用特殊光照模式渲染
    // flag第三位为1，渲染成网格
    m_materialUBO.flags = {1u, 1u, 1u, 0u}; 
    CreateMaterialParamSetsForSubmesh(m_id, 0, m_materialUBO);

    return plane;
}
}  // namespace entity