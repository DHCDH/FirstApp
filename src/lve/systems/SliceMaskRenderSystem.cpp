#include "SliceMaskRenderSystem.h"

#define GLM_FORCE_RADIANS  // 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  //深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <array>
#include <glm.hpp>
#include <gtc/constants.hpp>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace lve
{
struct SlicePushConstants {
    glm::mat4 modelMatrix;
    float yM;
    float thickness;
};

SliceMaskRenderSystem::SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass,
                                             VkDescriptorSetLayout setLayout)
    : m_lveDevice(device)
{
    CreatePipelineLayout(setLayout);  // 定义渲染管线的layout
    CreatePipelines(renderPass);
}

SliceMaskRenderSystem::~SliceMaskRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

/* 创建渲染管线
 * 告诉vulkan渲染管线在执行时可以用哪些数据
 */
void SliceMaskRenderSystem::CreatePipelineLayout(const VkDescriptorSetLayout& setLayout)
{
    /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
     * 能被VS和FS两个阶段可见
     */
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SlicePushConstants);

    /*描述符集*/
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;  // 描述符集布局数量（descriptor set layouts）
    pipelineLayoutInfo.pSetLayouts = &setLayout;  // 指向布局数组的指针
    /*把push constant范围装入管线布局*/
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    /*创建管线布局对象*/
    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void SliceMaskRenderSystem::CreatePipelines(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr &&
           "Cannot create pipeline before pipeline layout");

    CreateBlankStencilPipeline(renderPass);
    CreateBlankColorPipeline(renderPass);
    CreateWheelStencilPipeline(renderPass);
    CreateWheelColorPipeline(renderPass);
}

void SliceMaskRenderSystem::CreateBlankStencilPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    /*顶点输入：只绑定Position*/
    auto allAttributes = LveModel::Vertex::GetAttributeDescriptions();
    config.attributeDescriptions.clear();
    config.attributeDescriptions.push_back(allAttributes[0]);
    config.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();

    /*光栅化：关闭背面剔除*/
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    /*颜色混合：关闭写入*/
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写任何颜色

    /*深度/模板：关闭深度，开启模板*/
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 1;
    config.depthStencilInfo.front.compareMask = 1;
    config.depthStencilInfo.back.writeMask = 1;
    config.depthStencilInfo.back.compareMask = 1;

    /*正反面翻转*/
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INVERT;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_blankStencilPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_blank.vert.spv",
        "../../../res/shaders/spv/shader_slice_blank.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateBlankColorPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    /*顶点输入*/
    auto allAttributes = LveModel::Vertex::GetAttributeDescriptions();
    config.attributeDescriptions.clear();
    config.attributeDescriptions.push_back(allAttributes[0]);
    config.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();

    /*光栅化*/
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    /*颜色混合：关闭Blend，开启Logic Op*/
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0xF;  // RGBA

    /*开启Logoc Op*/
    config.colorBlendInfo.logicOpEnable = VK_TRUE;
    config.colorBlendInfo.logicOp = VK_LOGIC_OP_OR;

    /*深度 / 模版：只在 Stencil == 1 处绘制*/
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareMask = 1;
    config.depthStencilInfo.back.compareMask = 1;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
    config.depthStencilInfo.front.reference = 1;  // 必须是 1 (实心)
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_blankColorPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_blank.vert.spv",
        "../../../res/shaders/spv/shader_slice_blank.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateWheelStencilPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    /*binding = 0: position only*/
    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> posAttr;
    posAttr.push_back(attributeDescs[0]);
    attributeDescs = posAttr;

    /*binding = 1: instance matrix*/
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(glm::mat4);
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindingDescs.push_back(instanceBinding);

    for (uint32_t i = 0; i < 4; i++) {
        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 1;
        attribute.location = i + 4;
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribute.offset = sizeof(glm::vec4) * i;
        attributeDescs.push_back(attribute);
    }
    config.bindingDescriptions = bindingDescs;
    config.attributeDescriptions = attributeDescs;

    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0xFF;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.back.writeMask = 0xFF;
    config.depthStencilInfo.back.compareMask = 0xFF;
    /*正面（光线进入）-> 计数+1*/
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.back = config.depthStencilInfo.front;
    /*背面（光线穿出）-> 计数-1*/
    config.depthStencilInfo.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    config.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;
    /*其他Op*/
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back.depthFailOp = VK_STENCIL_OP_KEEP;

    m_grndWheelStencilPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_instanced.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateWheelColorPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    /*binding = 0: position only*/
    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> posAttr;
    posAttr.push_back(attributeDescs[0]);
    attributeDescs = posAttr;

    /*binding = 1: instance matrix*/
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(glm::mat4);
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindingDescs.push_back(instanceBinding);

    for (uint32_t i = 0; i < 4; i++) {
        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 1;
        attribute.location = i + 4;
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribute.offset = sizeof(glm::vec4) * i;
        attributeDescs.push_back(attribute);
    }
    config.bindingDescriptions = bindingDescs;
    config.attributeDescriptions = attributeDescs;

    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0xF;  // RGBA
    config.colorBlendInfo.logicOpEnable = VK_TRUE;
    config.colorBlendInfo.logicOp = VK_LOGIC_OP_OR;

    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.back.compareMask = 0xFF;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    config.depthStencilInfo.front.reference = 0;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_grndWheelColorPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_instanced.frag.spv",
        config);
}

void SliceMaskRenderSystem::BindBlankStencilPipeline(VkCommandBuffer commandBuffer)
{
    m_blankStencilPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindBlankColorPipeline(VkCommandBuffer commandBuffer)
{
    m_blankColorPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindGrindingWheelStencilPipeline(VkCommandBuffer commandBuffer)
{
    m_grndWheelStencilPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindGrindingWheelColorPipeline(VkCommandBuffer commandBuffer)
{
    m_grndWheelColorPipeline->Bind(commandBuffer);
}

/*调用Render前，由调用者绑定对应pipeline*/
void SliceMaskRenderSystem::RenderBlank(const SliceInfo& sliceMaskInfo)
{
    // 先绑定 set=0（全局 UBO），每帧一次
    vkCmdBindDescriptorSets(sliceMaskInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &sliceMaskInfo.globalDescriptorSet,
                            0,
                            nullptr);

    SlicePushConstants push{};
    push.modelMatrix = sliceMaskInfo.modelMatrix;
    push.yM = sliceMaskInfo.yM;
    push.thickness = sliceMaskInfo.thickness;

    vkCmdPushConstants(sliceMaskInfo.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    sliceMaskInfo.model.Bind(sliceMaskInfo.commandBuffer);
    sliceMaskInfo.model.Draw(sliceMaskInfo.commandBuffer);
}

/*调用Render前，由调用者绑定对应pipeline*/
void SliceMaskRenderSystem::RenderGrindingWheelInstances(const SliceInstancedInfo& info)
{
    if (info.instanceCount == 0) {
        std::cout << "No grinding wheel instances to render!" << std::endl;
        return;
    }

    vkCmdBindDescriptorSets(info.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &info.globalDescriptorSet,
                            0,
                            nullptr);

    SlicePushConstants push{};
    push.modelMatrix = glm::mat4(1.0);  // 占位
    push.yM = info.yM;
    push.thickness = info.thickness;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    /*binding = 0*/
    info.model.Bind(info.commandBuffer);

    /*binding = 1*/
    VkBuffer instanceBuffers[] = {info.instanceBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(info.commandBuffer, 1, 1, instanceBuffers, offsets);

    /*draw*/
    info.model.DrawInstanced(info.commandBuffer, info.instanceCount);
}

}  // namespace lve