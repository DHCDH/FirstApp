#include "GrindingWheelPoseRenderSystem.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <glm.hpp>
#include <iostream>
#include <stdexcept>

using namespace lve;

GrindingWheelPoseRenderSystem::GrindingWheelPoseRenderSystem(
    LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout,
    VkDescriptorSetLayout ssboSetLayout)
    : m_lveDevice(device)
{
    CreatePipelineLayout(globalSetLayout, ssboSetLayout);
    CreateStencilFrontPipeline(renderPass);
    CreateStencilBackPipeline(renderPass);
    CreateStencilResolvePipeline(renderPass);
    CreateStencilClearPipeline(renderPass);
}

void GrindingWheelPoseRenderSystem::CreatePipelineLayout(
    VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout ssboSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(BatchedWheelPushConstants);

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount =
        static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create procedural wheel pipeline layout!");
    }
}

void GrindingWheelPoseRenderSystem::CreatePipeline(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr &&
           "Cannot create pipeline before pipeline layout");

    PipelineConfigInfo pipelineConfig{};
    LvePipeline::defaultPipelineConfigInfo(pipelineConfig);
}

GrindingWheelPoseRenderSystem::~GrindingWheelPoseRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}