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
    float xM;
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
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT |
                                   VK_SHADER_STAGE_GEOMETRY_BIT;
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
    CreateBlankDepthPipeline(renderPass);
    CreateBlankColorPipeline(renderPass);
    CreateGrindingWheelStencilFrontPipeline(renderPass);
    CreateGrindingWheelStencilBackPipeline(renderPass);
    CreateGrindingWheelWireframePipeline(renderPass);

    CreatePlaneInjectionPipeline(renderPass);
    CreateGrindingWheelEdgePipeline(renderPass);

    // CreateSliceContourPipeline(renderPass);
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

    // 关闭深度测试与写入，只需要生成stencil mask，不污染深度缓冲
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x01;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.back.writeMask = 1;
    config.depthStencilInfo.back.compareMask = 1;

    /*正反面翻转*/
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INVERT;
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

void SliceMaskRenderSystem::CreateGrindingWheelStencilFrontPipeline(
    VkRenderPass renderPass)
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

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x02;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.front.reference = 0x02;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_REPLACE;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;

    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_grndWheelStencilFrontPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_instanced.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateGrindingWheelStencilBackPipeline(
    VkRenderPass renderPass)
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

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x04;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.front.reference = 0x04;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_REPLACE;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;

    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_grndWheelStencilBackPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_instanced.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateGrindingWheelWireframePipeline(VkRenderPass renderPass)
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

    /*开启线框模式*/
    config.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
    config.rasterizationInfo.lineWidth = 1.f;
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0xF;  // RGBA
    config.colorBlendInfo.logicOpEnable = VK_TRUE;
    config.colorBlendInfo.logicOp = VK_LOGIC_OP_OR;

    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    config.depthStencilInfo.front.reference = 0;
    config.depthStencilInfo.front.compareMask = 0xFF;
    /*线框只读，不能修改模板缓冲里的数值*/
    config.depthStencilInfo.front.writeMask = 0;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;

    config.depthStencilInfo.back = config.depthStencilInfo.front;
    config.depthStencilInfo.back.writeMask = 0;

    m_grndWheelWireframePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_instanced.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateBlankDepthPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    auto allAttributes = LveModel::Vertex::GetAttributeDescriptions();
    config.attributeDescriptions.clear();
    config.attributeDescriptions.push_back(allAttributes[0]);
    config.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();

    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0;

    // 【关键差异】：开启深度写入，但模版操作全为 KEEP
    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_TRUE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;  // 与 BlankStencil 一致

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0;  // 不写模版
    config.depthStencilInfo.front.compareMask = 0;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;  // 保持不变
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_blankDepthPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_blank.vert.spv",
        "../../../res/shaders/spv/shader_slice_blank.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreatePlaneInjectionPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    // vertex shader使用gl_VertexIndex生成三角形，不需要VBO
    config.attributeDescriptions.clear();
    config.bindingDescriptions.clear();

    // 关闭颜色写入
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0;

    // 关闭提出，确保全屏三角形一定显示
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    // 关闭模板测试
    config.depthStencilInfo.stencilTestEnable = VK_FALSE;

    // 强制写入深度
    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_TRUE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    // 只关注砂轮截形，暂时关闭模板测试
    config.depthStencilInfo.stencilTestEnable = VK_FALSE;

    // 创建pipeline
    m_planeInjectionPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_plane.vert.spv",
        "../../../res/shaders/spv/shader_slice_plane.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateGrindingWheelEdgePipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> posAttr;
    posAttr.push_back(attributeDescs[0]);
    attributeDescs = posAttr;

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

    // 光栅化：关闭剔除，需要看到所有面的截线
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    // 颜色写入：开启，写入到Attachment0
    config.colorBlendAttachment.blendEnable = VK_FALSE;
    config.colorBlendAttachment.colorWriteMask = 0xF;

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_TRUE;
    config.depthStencilInfo.stencilTestEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    m_grndWheelEdgePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_instanced.vert.spv",
        "../../../res/shaders/spv/shader_slice_edge.frag.spv",
        config);
}

void SliceMaskRenderSystem::CreateSliceContourPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();

    std::vector<VkVertexInputAttributeDescription> posAttr;
    posAttr.push_back(attributeDescs[0]);
    attributeDescs = posAttr;

    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(glm::mat4);
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindingDescs.push_back(instanceBinding);

    for (uint32_t i = 0; i < 4; i++) {
        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 1;
        attribute.location = i + 4;  // Location 4,5,6,7
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribute.offset = sizeof(glm::vec4) * i;
        attributeDescs.push_back(attribute);
    }
    config.bindingDescriptions = bindingDescs;
    config.attributeDescriptions = attributeDescs;

    // 输入图元拓扑：三角形
    config.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 光栅化设置
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
    config.rasterizationInfo.lineWidth = 1.1f;

    // 深度/模板
    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_TRUE;  // 写入深度，遮挡后面的东西
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    config.depthStencilInfo.stencilTestEnable = VK_FALSE;

    config.colorBlendAttachment.blendEnable = VK_FALSE;  // 不透明
    config.colorBlendAttachment.colorWriteMask = 0xF;    // 写入 RGBA

    // 动态状态 (可选)：允许运行时通过 vkCmdSetLineWidth 改变线宽
    config.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
    config.dynamicStateInfo.pDynamicStates = config.dynamicStateEnables.data();
    config.dynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(config.dynamicStateEnables.size());

    m_sliceContourPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_geom.vert.spv",
        "../../../res/shaders/spv/shader_slice_geom.frag.spv",
        config,
        "../../../res/shaders/spv/shader_slice_geom.geom.spv");
}

void SliceMaskRenderSystem::BindBlankDepthPipeline(VkCommandBuffer commandBuffer)
{
    m_blankDepthPipeline->Bind(commandBuffer);
}

void SliceMaskRenderSystem::BindBlankStencilPipeline(VkCommandBuffer commandBuffer)
{
    m_blankStencilPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindBlankColorPipeline(VkCommandBuffer commandBuffer)
{
    m_blankColorPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindGrindingWheelStencilFrontPipeline(
    VkCommandBuffer commandBuffer)
{
    m_grndWheelStencilFrontPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindGrindingWheelStencilBackPipeline(
    VkCommandBuffer commandBuffer)
{
    m_grndWheelStencilBackPipeline->Bind(commandBuffer);
}
void SliceMaskRenderSystem::BindGrindingWheelWireframePipeline(
    VkCommandBuffer commandBuffer)
{
    m_grndWheelWireframePipeline->Bind(commandBuffer);
}

void SliceMaskRenderSystem::BindPlaneInjectionPipeline(VkCommandBuffer commandBuffer)
{
    m_planeInjectionPipeline->Bind(commandBuffer);
}

void SliceMaskRenderSystem::BindGrindingWheelEdgePipeline(VkCommandBuffer commandBuffer)
{
    m_grndWheelEdgePipeline->Bind(commandBuffer);
}

void SliceMaskRenderSystem::BindSliceContourPipeline(VkCommandBuffer commandBuffer)
{
    m_sliceContourPipeline->Bind(commandBuffer);
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
    push.xM = sliceMaskInfo.xM;
    push.thickness = sliceMaskInfo.thickness;

    vkCmdPushConstants(sliceMaskInfo.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
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
    push.xM = info.xM;
    push.thickness = info.thickness;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
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

void SliceMaskRenderSystem::RenderPlaneInjection(VkCommandBuffer commandBuffer,
                                                 VkDescriptorSet globalDescriptorSet,
                                                 float xM)
{
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &globalDescriptorSet,
                            0,
                            nullptr);

    SlicePushConstants push{};
    push.modelMatrix = glm::mat4(1.0);  // 占位
    push.xM = xM;
    push.thickness = 0.f;
    vkCmdPushConstants(commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    // 绘制全屏三角形
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void SliceMaskRenderSystem::RenderSliceContour(const SliceInstancedInfo& info)
{
    if (info.instanceCount == 0) {
        std::cout << "No slice contour instances to render!" << std::endl;
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

    // 推送常量
    SlicePushConstants push{};
    push.modelMatrix = glm::mat4(1.0);
    push.xM = info.xM;
    push.thickness = info.thickness;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    // 设置线宽
    vkCmdSetLineWidth(info.commandBuffer, 1.1f);

    // 绑定顶点和实例
    info.model.Bind(info.commandBuffer);
    VkBuffer instanceBuffers[] = {info.instanceBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(info.commandBuffer, 1, 1, instanceBuffers, offsets);

    info.model.DrawInstanced(info.commandBuffer, info.instanceCount);
}

}  // namespace lve