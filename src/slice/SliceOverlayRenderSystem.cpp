#include "SliceOverlayRenderSystem.h"

namespace lve
{
struct SlicePushConstants {
    glm::mat4 modelMatrix;
    alignas(16) glm::vec3 normal;
    alignas(16) glm::vec3 point;
};

SliceOverlayRenderSystem::SliceOverlayRenderSystem(LveDevice& device,
                                                   VkRenderPass swapChainRenderPass,
                                                   VkDescriptorSetLayout globalSetLayout)
    : m_lveDevice(device)
{
    CreatePipelineLayout(globalSetLayout);
    CreatePipeline(swapChainRenderPass);
}

SliceOverlayRenderSystem::~SliceOverlayRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

void SliceOverlayRenderSystem::CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT |
                                   VK_SHADER_STAGE_GEOMETRY_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SlicePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &globalSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create overlay pipeline layout!");
    }
}

void SliceOverlayRenderSystem::CreatePipeline(VkRenderPass renderPass)
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

    m_pipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_slice_geom.vert.spv",
        "../../../res/shaders/spv/shader_slice_geom.frag.spv",
        config,
        "../../../res/shaders/spv/shader_slice_geom.geom.spv");
}

void SliceOverlayRenderSystem::RenderSliceContour(const SliceInstancedInfo& info)
{
    if (info.instanceCount == 0) {
        std::cout << "No slice contour instances to render!" << std::endl;
        return;
    }

    m_pipeline->Bind(info.commandBuffer);

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

}  // namespace lve