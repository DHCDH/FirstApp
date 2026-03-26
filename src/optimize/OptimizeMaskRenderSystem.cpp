#include "OptimizeMaskRenderSystem.h"

using namespace lve;

namespace optimize
{
struct OptimizePushConstants {
    glm::mat4 modelMatrix;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;
};

struct OptimizeComputePushConstants {
    uint32_t maxPoints = 100000;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;
    alignas(16) glm::vec4 mapInfo;  // 映射参数：xMin, zMin, dx, dz
    int isRightCut{1};
};

OptimizeMaskRenderSystem::OptimizeMaskRenderSystem(
    lve::LveDevice& device, VkRenderPass renderPass,
    VkDescriptorSetLayout graphicsSetLayout, VkDescriptorSetLayout computeSetLayout,
    VkDescriptorSetLayout bboxSetLayout)
    : m_lveDevice{device}
{
    CreatePipelineLayout(graphicsSetLayout);
    CreateComputePipelineLayout(computeSetLayout, graphicsSetLayout);
    CreateBBoxPipelineLayout(bboxSetLayout);

    CreateBlankStencilPipeline(renderPass);
    CreateBlankColorPipeline(renderPass);
    CreatePlaneInjectionPipeline(renderPass);
    CreateComputePipeline();
    CreateBBoxPipeline();
}

void OptimizeMaskRenderSystem::CreatePipelineLayout(
    const VkDescriptorSetLayout& setLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(OptimizePushConstants);

    // --- 描述符集 ---
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

void OptimizeMaskRenderSystem::CreateComputePipelineLayout(
    const VkDescriptorSetLayout& computeSetLayout,
    const VkDescriptorSetLayout& graphicsSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(OptimizeComputePushConstants);

    std::array<VkDescriptorSetLayout, 2> setLayouts{computeSetLayout, graphicsSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }
}

void OptimizeMaskRenderSystem::CreateBBoxPipelineLayout(
    const VkDescriptorSetLayout& bboxSetLayout)
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &bboxSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_bboxPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }
}

void OptimizeMaskRenderSystem::CreateBlankStencilPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    // 顶点输入：只绑定Position
    auto allAttributes = LveModel::Vertex::GetAttributeDescriptions();
    config.attributeDescriptions.clear();
    config.attributeDescriptions.push_back(allAttributes[0]);
    config.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();

    // 光栅化：关闭背面剔除
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    // 颜色混合：关闭写入
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

    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INVERT;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_blankStencilPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_blank.vert.spv",
        "../../../res/shaders/spv/optimize/shader_optimize_blank.frag.spv",
        config);
}

void OptimizeMaskRenderSystem::CreateBlankColorPipeline(VkRenderPass renderPass)
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
    config.colorBlendAttachment.colorWriteMask = 0xF;
    config.colorBlendInfo.logicOpEnable = VK_TRUE;
    config.colorBlendInfo.logicOp = VK_LOGIC_OP_OR;

    config.depthStencilInfo.depthTestEnable = VK_TRUE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareMask = 0x80;
    config.depthStencilInfo.front.reference = 0x80;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    m_blankColorPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_blank.vert.spv",
        "../../../res/shaders/spv/optimize/shader_optimize_blank.frag.spv",
        config);
}

void OptimizeMaskRenderSystem::CreatePlaneInjectionPipeline(VkRenderPass renderPass)
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
        "../../../res/shaders/spv/optimize/shader_optimize_plane.vert.spv",
        "../../../res/shaders/spv/optimize/shader_optimize_plane.frag.spv",
        config);
}

void OptimizeMaskRenderSystem::CreateComputePipeline()
{
    assert(m_computePipelineLayout != nullptr &&
           "Cannot create compute pipeline before compute pipeline layout");

    PipelineConfigInfo configInfo{};
    configInfo.pipelineLayout = m_computePipelineLayout;

    m_extractContourPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_extract_contour.comp.spv",
        configInfo);

    m_knnPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_sort_contour.comp.spv",
        configInfo);

    m_tracePipeline =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/optimize/shader_trace.comp.spv",
                                      configInfo);

    m_alignPipeline =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/optimize/shader_align.comp.spv",
                                      configInfo);

    m_rakeAnglePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_rake_angle.comp.spv",
        configInfo);
}

void OptimizeMaskRenderSystem::CreateBBoxPipeline()
{
    PipelineConfigInfo configInfo{};
    configInfo.pipelineLayout = m_bboxPipelineLayout;
    m_bboxPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/shader_optimize_find_bbox.comp.spv",
        configInfo);
}

void OptimizeMaskRenderSystem::BindPlaneInjectionPipeline(VkCommandBuffer commandBuffer)
{
    m_planeInjectionPipeline->Bind(commandBuffer);
}

void OptimizeMaskRenderSystem::BindBlankStencilPipeline(VkCommandBuffer commandBuffer)
{
    m_blankStencilPipeline->Bind(commandBuffer);
}

void OptimizeMaskRenderSystem::BindBlankColorPipeline(VkCommandBuffer commandBuffer)
{
    m_blankColorPipeline->Bind(commandBuffer);
}

void OptimizeMaskRenderSystem::RenderPlaneInjection(const OptimizePlaneInfo& info)
{
    vkCmdBindDescriptorSets(info.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &info.globalDescriptorSet,
                            0,
                            nullptr);
    OptimizePushConstants push{};
    push.modelMatrix = glm::mat4(1.0f);
    push.normal = info.normal;
    push.point = info.point;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(OptimizePushConstants),
                       &push);

    vkCmdDraw(info.commandBuffer, 3, BATCH_LAYER_COUNT, 0, 0);
}

void OptimizeMaskRenderSystem::RenderBlank(const OptimizeDrawInfo& info)
{
    vkCmdBindDescriptorSets(info.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &info.globalDescriptorSet,
                            0,
                            nullptr);
    OptimizePushConstants push{};
    push.modelMatrix = info.modelMatrix;
    push.normal = info.normal;
    push.point = info.point;
    vkCmdPushConstants(info.commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(OptimizePushConstants),
                       &push);

    info.model.Bind(info.commandBuffer);

    info.model.DrawInstanced(info.commandBuffer, BATCH_LAYER_COUNT, 0);
}

void OptimizeMaskRenderSystem::ComputeBBox(VkCommandBuffer commandBuffer,
                                           VkDescriptorSet bboxDescriptorSet,
                                           uint32_t width, uint32_t height,
                                           uint32_t planeIdx)
{
    m_bboxPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_bboxPipelineLayout,
                            0,
                            1,
                            &bboxDescriptorSet,
                            0,
                            nullptr);

    uint32_t groupX = (width + 15) / 16;
    uint32_t groupY = (height + 15) / 16;

    vkCmdDispatch(commandBuffer, groupX, groupY, BATCH_LAYER_COUNT);
}

void OptimizeMaskRenderSystem::ComputeFlute(VkCommandBuffer commandBuffer,
                                            const SliceComputeInfo& computeInfo,
                                            VkDescriptorSet globalDescriptorSet,
                                            lve::LveBuffer* tipInfoBuffer)
{
    vkCmdFillBuffer(commandBuffer,
                    tipInfoBuffer->GetBuffer(),
                    0,
                    sizeof(uint32_t) * 2 * BATCH_LAYER_COUNT,
                    0);

    m_extractContourPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    OptimizeComputePushConstants push{computeInfo.maxPoints,
                                      computeInfo.normal,
                                      computeInfo.point,
                                      computeInfo.mapInfo,
                                      0};

    std::array<VkDescriptorSet, 2> sets{computeInfo.descriptorSet, globalDescriptorSet};
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            static_cast<uint32_t>(sets.size()),
                            sets.data(),
                            0,
                            nullptr);
    vkCmdPushConstants(commandBuffer,
                       m_computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(OptimizeComputePushConstants),
                       &push);

    uint32_t groupCountX = (computeInfo.width + 15) / 16;
    uint32_t groupCountY = (computeInfo.height + 15) / 16;

    // 💥 第一步特征提取：Z 轴并发 256 次！
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, BATCH_LAYER_COUNT);

    VkMemoryBarrier stageBarrier{};
    stageBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    stageBarrier.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    stageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1,
        &stageBarrier,
        0,
        nullptr,
        0,
        nullptr);

    VkMemoryBarrier computeBarrier{};
    computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    auto insert_compute_barrier = [&]() {
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &computeBarrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
    };

    // KNN 建图：X 轴管点数，Z 轴并发 256 次
    uint32_t groupCount = (computeInfo.maxPoints + 255) / 256;
    m_knnPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(commandBuffer, groupCount, 1, BATCH_LAYER_COUNT);  // 👈 挪到 Z 轴
    insert_compute_barrier();

    // Trace 单线程寻迹：X、Y=1，Z 轴并发 256 次
    m_tracePipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(commandBuffer, 1, 1, BATCH_LAYER_COUNT);  // 👈 挪到 Z 轴
    insert_compute_barrier();

    // Align 对齐：X 轴管点数，Z 轴并发 256 次
    m_alignPipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(commandBuffer, groupCount, 1, BATCH_LAYER_COUNT);  // 👈 挪到 Z 轴
    insert_compute_barrier();

    // RakeAngle 前角计算：X、Y=1，Z 轴并发 256 次
    m_rakeAnglePipeline->Bind(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(commandBuffer, 1, 1, BATCH_LAYER_COUNT);
}

OptimizeMaskRenderSystem::~OptimizeMaskRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_lveDevice.device(), m_computePipelineLayout, nullptr);
    }
    if (m_bboxPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_lveDevice.device(), m_bboxPipelineLayout, nullptr);
    }
}

}  // namespace optimize