#include "Cube.h"

namespace entity
{
lve::LveObject Cube::CreateObject()
{
    p_model = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    auto cube = lve::LveObject::CreateObject();
    cube.model = p_model;
    cube.transform.translation = {0.f, 0.f, 0.f};
    cube.transform.rotation = {0.f, 0.f, 0.f};
    cube.transform.scale = {.5f, .5f, .5f};

    // 设置透明度
    cube.transparency = .7f;

    m_id = cube.getId();

    auto subCount = cube.model->GetSubmeshCount();
    GetRenderContext().submeshTextureSets[m_id].resize(subCount);
    GetRenderContext().submeshMatSets[m_id].resize(subCount);

    m_materialUBO.baseColorFactor = {0.f, 0.f, 0.f, 1.f};
    // flag第一位为2，表示cube
    // flag第四位为1，需要渲染线框
    m_materialUBO.flags = {2u, 0u, 0u, 0u};
    CreateMaterialParamSetsForSubmesh(m_id, 0, m_materialUBO);

    return cube;
}
}  // namespace entity