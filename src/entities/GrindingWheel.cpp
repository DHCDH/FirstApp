#include "GrindingWheel.h"

#include "LveModel.h"

GrindingWheel::GrindingWheel(lve::LveDevice& device, lve::LveModel& model)
	: m_lveDevice{ device }, m_lveModel{ model }
{

}

lve::LveObject GrindingWheel::CreateObject()
{
	std::shared_ptr<lve::LveModel> lveModel = lve::LveModel::CreateModelFromFile(m_lveDevice, m_filepath);
	auto grindingWheel = lve::LveObject::CreateObject();
	grindingWheel.model = lveModel;
	grindingWheel.transform.translation = m_controller.GetStartPose().position;
	grindingWheel.transform.rotation = m_controller.GetStartPose().orientation;

	m_id = grindingWheel.getId();
	uint32_t wheelId = grindingWheel.getId();
	auto subCount = grindingWheel.model->GetSubmeshCount();

	return grindingWheel;
}

void GrindingWheel::Update()
{
	
}