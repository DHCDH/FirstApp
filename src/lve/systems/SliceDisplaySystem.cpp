#include "SliceDisplaySystem.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <glm.hpp>
#include <gtc/constants.hpp>
#include <iostream>
#include <stdexcept>

namespace lve
{
SliceDisplaySystem::SliceDisplaySystem(LveDevice& device, VkRenderPass renderPass,
                                       VkDescriptorSetLayout displaySetLayout)
    : m_lveDevice{device}
{
    CreatePipelineLayout(displaySetLayout);
    CreatePipeline(renderPass);
}

SliceDisplaySystem::~SliceDisplaySystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

void SliceDisplaySystem::CreatePipelineLayout(VkDescriptorSetLayout displaySetLayout)
{
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts{displaySetLayout};

    /*描述符集*/
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(
        descriptorSetLayouts.size());  // 描述符集布局数量（descriptor set layouts）
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();  // 指向布局数组的指针
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    /*创建管线布局对象*/
    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void SliceDisplaySystem::CreatePipeline(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr &&
           "Cannot create pipeline before pipeline layout");

    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;

    pipelineConfig.depthStencilInfo.depthTestEnable = VK_FALSE;
    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
    pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;  // 关闭背面剔除
    /*清空定点输入描述*/
    pipelineConfig.attributeDescriptions.clear();
    pipelineConfig.bindingDescriptions.clear();

    m_lvePipeline =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/shader_slice_display.vert.spv",
                                      "../../../res/shaders/spv/shader_slice_display.frag.spv",
                                      pipelineConfig);
}

void SliceDisplaySystem::Render(VkCommandBuffer commandBuffer,
    VkDescriptorSet displayDescriptorSet)
{
    m_lvePipeline->Bind(commandBuffer);

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &displayDescriptorSet,
                            0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

}  // namespace lve