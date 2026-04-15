#pragma once

#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "../Global.h"

#include <glm.hpp>
#include <string>
#include <memory>

namespace entity
{
class Entity
{
public:
    Entity(RenderContext& renderContext) : m_renderContext(renderContext)
    {
    }
    virtual lve::LveObject CreateObject() = 0;

    lve::LveModel* GetModel() const
    {
        return p_model.get();
    }
    lve::MaterialUBO GetMaterialUBO() const
    {
        return m_materialUBO;
    }


protected:
    void CreateMaterialParamSetsForSubmesh(uint32_t objId, uint32_t submeshIndex,
                                           const lve::MaterialUBO& u);
    void AssignMaterialParamsToObject(uint32_t objId, const lve::MaterialUBO& init);
    RenderContext& GetRenderContext()
    {
        return m_renderContext;
    }

    std::shared_ptr<lve::LveModel> p_model;

    lve::MaterialUBO m_materialUBO;

private:
    RenderContext& m_renderContext;
};
}  // namespace entity