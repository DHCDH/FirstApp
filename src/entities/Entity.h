#pragma once

#include "LveDevice.h"
#include "LveModel.h"
#include "LveObject.h"
#include "../Global.h"

#include <glm.hpp>
#include <string>
#include <memory>

class Entity {
public:
	Entity(RenderContext& renderContext) : m_renderContext(renderContext) {}
	virtual lve::LveObject CreateObject() = 0;

	lve::LveModel* GetModel() const { return p_model.get(); }

protected:
	void CreateMaterialParamSetsForSubmesh(uint32_t objId, uint32_t submeshIndex, const lve::MaterialUBO& u);
	void AssignMaterialParamsToObject(uint32_t objId, const lve::MaterialUBO& init);
	RenderContext& GetRenderContext() { return m_renderContext; }

	std::shared_ptr<lve::LveModel> p_model;

private:
	RenderContext& m_renderContext;
	
};