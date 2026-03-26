#include "OptimizePoseRenderSystem.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <glm.hpp>
#include <iostream>
#include <stdexcept>

using namespace lve;

namespace optimize
{
OptimizePoseRenderSystem::OptimizePoseRenderSystem(LveDevice& device,
                                                   LveModel& grndWheelModel,
                                                   VkRenderPass renderPass,
                                                   VkDescriptorSetLayout globalSetLayout,
                                                   VkDescriptorSetLayout ssboSetLayout)
    : m_lveDevice(device), m_grndWheelModel(grndWheelModel)
{
    CreatePipelineLayout(globalSetLayout, ssboSetLayout);
    CreateStencilFrontPipeline(renderPass);
    CreateStencilBackPipeline(renderPass);
    CreateStencilResolvePipeline(renderPass);
    CreateStencilClearPipeline(renderPass);
}

void OptimizePoseRenderSystem::CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout,
                                                    VkDescriptorSetLayout ssboSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    //pushConstantRange.size = sizeof(BatchedWheelPushConstants);
    pushConstantRange.size = 128;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{globalSetLayout, ssboSetLayout};

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

void OptimizePoseRenderSystem::CreateStencilFrontPipeline(VkRenderPass renderPass)
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

    config.bindingDescriptions = bindingDescs;
    config.attributeDescriptions = attributeDescs;

    config.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色

    // 关闭深度测试
    config.depthStencilInfo.depthTestEnable = VK_FALSE;  // mark

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x7F;
    config.depthStencilInfo.front.compareMask = 0x7F;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;

    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_stencilFrontPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_generate_wheels.vert.spv",
        "../../../res/shaders/spv/optimize/shader_optimize_generate_wheels.frag.spv",
        config);
}

void OptimizePoseRenderSystem::CreateStencilBackPipeline(VkRenderPass renderPass)
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

    config.bindingDescriptions = bindingDescs;
    config.attributeDescriptions = attributeDescs;

    config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色

    // 关闭深度测试
    config.depthStencilInfo.depthTestEnable = VK_FALSE;  // MARK

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x7F;
    config.depthStencilInfo.front.compareMask = 0x7F;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;

    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_stencilBackPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_generate_wheels.vert.spv",
        "../../../res/shaders/spv/optimize/shader_optimize_generate_wheels.frag.spv",
        config);
}

void OptimizePoseRenderSystem::CreateStencilResolvePipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    // 无顶点输入 (全屏三角形由 Vertex Shader 生成)
    config.attributeDescriptions.clear();
    config.bindingDescriptions.clear();
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    // 模板测试：只检测低 7 位 (0x7F) 是否非零
    // 逻辑：如果 (Stencil & 0x7F) != 0，则通过测试，执行 Fragment Shader
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    config.depthStencilInfo.front.reference = 0;
    config.depthStencilInfo.front.compareMask = 0x7F;  // 只读取砂轮的计数位
    config.depthStencilInfo.front.writeMask = 0;       // 只读，不写 Stencil
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    // 颜色混合：开启 LogicOp OR
    // 目的：如果像素已经被标记为 2 (例如之前的批次)，再次写入 2 结果还是 2。
    // 如果之前是 0，写入 2 变成 2。如果是 1 (毛坯)， 1 | 2 = 3 (交叉区域)。
    // 假设 Format 是 R32_UINT，LogicOp 是处理整数附件的最佳方式。
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendInfo.logicOpEnable = VK_TRUE;
    config.colorBlendInfo.logicOp = VK_LOGIC_OP_OR;
    config.colorBlendAttachment.colorWriteMask = 0xF;

    m_stencilResolvePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_plane.vert.spv",  // 复用现有的全屏
                                                                             // VS
        "../../../res/shaders/spv/optimize/shader_optimize_resolve.frag.spv",  // 新建的
                                                                               // FS
        config);
}

void OptimizePoseRenderSystem::CreateStencilClearPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    config.attributeDescriptions.clear();
    config.bindingDescriptions.clear();
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    // 关闭深度、关闭颜色写入
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色

    // 模板操作：Always Pass，然后 Replace 为 0
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.reference = 0;  // 目标值 0
    config.depthStencilInfo.front.compareMask = 0xFF;  // 比较掩码无所谓，因为是 Always
    config.depthStencilInfo.front.writeMask =
        0x7F;  // 关键：只允许修改低 7 位，保留 Bit 7 (毛坯)
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_REPLACE;  // 替换为 reference (0)
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_REPLACE;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_REPLACE;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_stencilClearPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_plane.vert.spv",  // 复用全屏
                                                                             // VS
        "../../../res/shaders/spv/optimize/shader_optimize_empty.frag.spv",  // 新建的空
                                                                             // FS
        config);
}

void OptimizePoseRenderSystem::RenderBatchWheels(
    VkCommandBuffer commandBuffer, const BatchedWheelPushConstants& pushData,
    uint32_t instanceCount, VkDescriptorSet globalDescriptorSet,
    VkDescriptorSet ssboDescriptorSet)
{
    if (instanceCount == 0) return;

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &globalDescriptorSet,
                            0,
                            nullptr);

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            1,
                            1,
                            &ssboDescriptorSet,
                            0,
                            nullptr);

    vkCmdPushConstants(commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(BatchedWheelPushConstants),
                       &pushData);

    m_grndWheelModel.Bind(commandBuffer);

    uint32_t totalInstances = instanceCount * pushData.stepsPerPose;

    m_stencilFrontPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
    m_grndWheelModel.DrawInstanced(commandBuffer, totalInstances, 0);

    m_stencilBackPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
    m_grndWheelModel.DrawInstanced(commandBuffer, totalInstances, 0);

    m_stencilResolvePipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
    vkCmdDraw(commandBuffer, 3, instanceCount, 0, 0);

    m_stencilClearPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS);
    vkCmdDraw(commandBuffer, 3, instanceCount, 0, 0);
}

OptimizePoseRenderSystem::~OptimizePoseRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

}  // namespace optimize