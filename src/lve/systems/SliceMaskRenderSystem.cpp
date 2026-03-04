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
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;
};

struct SliceComputePushConstants {
    uint32_t maxPoints = 100000;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;
    alignas(16) glm::vec4 mapInfo;  // 映射参数：xMin, zMin, dx, dz
    int isRightCut{1};
};

SliceMaskRenderSystem::SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass,
                                             VkDescriptorSetLayout graphicsSetLayouts,
                                             VkDescriptorSetLayout computeSetLayouts)
    : m_lveDevice(device)
{
    CreatePipelineLayout(graphicsSetLayouts);  // 定义渲染管线的layout
    CreatePipelines(renderPass);

    CreateComputePipelineLayout(computeSetLayouts);
    CreateComputePipeline();
}

SliceMaskRenderSystem::~SliceMaskRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);

    // 销毁计算管线的布局句柄
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_lveDevice.device(), m_computePipelineLayout, nullptr);
    }
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

void SliceMaskRenderSystem::CreateComputePipelineLayout(
    const VkDescriptorSetLayout& setLayout)
{
    // 创建计算管线布局
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SliceComputePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
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

    CreateStencilResolvePipeline(renderPass);
    CreateStencilClearPipeline(renderPass);
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
    config.depthStencilInfo.front.writeMask = 0x80;
    config.depthStencilInfo.front.compareMask = 0x80;
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
    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareMask = 0x80;
    config.depthStencilInfo.front.reference = 0X80;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;

    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
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

    config.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x7F;
    config.depthStencilInfo.front.compareMask = 0x7F;
    // config.depthStencilInfo.front.reference = 0x02;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
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

    config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
    config.colorBlendAttachment.colorWriteMask = 0;  // 不写颜色

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.writeMask = 0x7F;
    config.depthStencilInfo.front.compareMask = 0x7F;
    // config.depthStencilInfo.front.reference = 0x04;

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
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

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
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

void SliceMaskRenderSystem::CreateStencilResolvePipeline(VkRenderPass renderPass)
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
        "../../../res/shaders/spv/shader_slice_plane.vert.spv",  // 复用现有的全屏 VS
        "../../../res/shaders/spv/shader_slice_resolve.frag.spv",  // 新建的 FS
        config);
}

void SliceMaskRenderSystem::CreateStencilClearPipeline(VkRenderPass renderPass)
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
        "../../../res/shaders/spv/shader_slice_plane.vert.spv",  // 复用全屏 VS
        "../../../res/shaders/spv/shader_slice_empty.frag.spv",  // 新建的空 FS
        config);
}

void SliceMaskRenderSystem::CreateComputePipeline()
{
    assert(m_computePipelineLayout != nullptr &&
           "Cannot create compute pipeline before compute pipeline layout");

    PipelineConfigInfo configInfo{};
    configInfo.pipelineLayout = m_computePipelineLayout;

    m_extractContourPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_extract_contour.comp.spv",
        configInfo);

    m_knnPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_sort_contour.comp.spv",
        configInfo);

    m_tracePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_trace.comp.spv",
        configInfo);

    m_alignPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_align.comp.spv",
        configInfo);

    m_rakeAnglePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_rake_angle.comp.spv",
        configInfo);
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

void SliceMaskRenderSystem::BindStencilResolvePipeline(VkCommandBuffer commandBuffer)
{
    m_stencilResolvePipeline->Bind(commandBuffer);
}

void SliceMaskRenderSystem::BindStencilClearPipeline(VkCommandBuffer commandBuffer)
{
    m_stencilClearPipeline->Bind(commandBuffer);
}

/*调用Render前，由调用者绑定对应pipeline*/
void SliceMaskRenderSystem::RenderBlank(const SliceDrawInfo& sliceDrawInfo)
{
    // 先绑定 set=0（全局 UBO），每帧一次
    vkCmdBindDescriptorSets(sliceDrawInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &sliceDrawInfo.globalDescriptorSet,
                            0,
                            nullptr);

    SlicePushConstants push{};
    push.modelMatrix = sliceDrawInfo.modelMatrix;
    push.normal = sliceDrawInfo.normal;
    push.point = sliceDrawInfo.point;

    vkCmdPushConstants(sliceDrawInfo.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    sliceDrawInfo.model.Bind(sliceDrawInfo.commandBuffer);
    sliceDrawInfo.model.Draw(sliceDrawInfo.commandBuffer);
}

/*调用Render前，由调用者绑定对应pipeline*/
void SliceMaskRenderSystem::RenderGrindingWheelInstances(const SliceInstancedInfo& info,
                                                         uint32_t firstInstance)
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
    push.normal = info.normal;
    push.point = info.point;

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
    info.model.DrawInstanced(info.commandBuffer, info.instanceCount, firstInstance);
}

void SliceMaskRenderSystem::RenderPlaneInjection(const SlicePlaneInfo& info)
{
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
    push.normal = info.normal;
    push.point = info.point;

    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    // 绘制全屏三角形
    vkCmdDraw(info.commandBuffer, 3, 1, 0, 0);
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
    push.normal = info.normal;
    push.point = info.point;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_GEOMETRY_BIT,
                       0,
                       sizeof(SlicePushConstants),
                       &push);

    // 设置线宽
    vkCmdSetLineWidth(info.commandBuffer, 0.1f);

    // 绑定顶点和实例
    info.model.Bind(info.commandBuffer);
    VkBuffer instanceBuffers[] = {info.instanceBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(info.commandBuffer, 1, 1, instanceBuffers, offsets);

    info.model.DrawInstanced(info.commandBuffer, info.instanceCount);
}

void SliceMaskRenderSystem::DispatchExtractContour(const SliceComputeInfo& info)
{
    // 告诉Vulkan绑定计算管线
    m_extractContourPipeline->Bind(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);

    // 绑定计算专用描述符集
    vkCmdBindDescriptorSets(info.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            1,
                            &info.descriptorSet,
                            0,
                            nullptr);

    // 推送平面参数
    SliceComputePushConstants push{info.maxPoints,
                                   info.normal,
                                   info.point,
                                   info.mapInfo,
                                   1};
    vkCmdPushConstants(info.commandBuffer,
                       m_computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(SliceComputePushConstants),
                       &push);

    // 计算派发组数量
    uint32_t groupCountX = (info.width + 15) / 16;
    uint32_t groupCountY = (info.height + 15) / 16;

    vkCmdDispatch(info.commandBuffer, groupCountX, groupCountY, 1);

    // --- 派发局部最小二乘前角拟合 ---
    // 插入屏障：等待对齐翻转把 finalPoints 写完
    VkMemoryBarrier alignBarrier{};
    alignBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    alignBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    alignBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(info.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &alignBarrier,
                         0,
                         nullptr,
                         0,
                         nullptr);

    m_rakeAnglePipeline->Bind(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);

    vkCmdDispatch(info.commandBuffer, 1, 1, 1);
}

void SliceMaskRenderSystem::DispatchTopologyReconstruction(const SliceComputeInfo& info,
                                                           bool isRightCut)
{
    vkCmdBindDescriptorSets(info.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            1,
                            &info.descriptorSet,
                            0,
                            nullptr);

    // 直接使用MAX_POINTS作为点数量。可用vkCmdDispatchIndirect进行优化
    uint32_t groupCount = (info.maxPoints + 255) / 256;

    // --- 派发KNN建图 ---
    m_knnPipeline->Bind(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(info.commandBuffer, groupCount, 1, 1);

    // --- 内部屏障，等待KNN写完 ---
    VkMemoryBarrier computeBarrier{};
    computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(info.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &computeBarrier,
                         0,
                         nullptr,
                         0,
                         nullptr);

    // --- 派发单线程追踪 ---
    m_tracePipeline->Bind(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(info.commandBuffer, 1, 1, 1);

    // 内部屏障，等待Trace写完
    vkCmdPipelineBarrier(info.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &computeBarrier,
                         0,
                         nullptr,
                         0,
                         nullptr);

    // --- 派发对齐翻转 ---
    m_alignPipeline->Bind(info.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(info.commandBuffer, groupCount, 1, 1);

}

}  // namespace lve