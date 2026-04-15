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
    plane.transparency = 0.2f;

    m_id = plane.getId();

    auto subCount = plane.model->GetSubmeshCount();
    GetRenderContext().submeshTextureSets[m_id].resize(subCount);
    GetRenderContext().submeshMatSets[m_id].resize(subCount);

    m_materialUBO.baseColorFactor = {0.f, 1.f, 1.f, .5f};
    CreateMaterialParamSetsForSubmesh(m_id, 0, m_materialUBO);

    return plane;
}
}  // namespace entity