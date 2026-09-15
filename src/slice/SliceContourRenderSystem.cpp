#include "SliceContourRenderSystem.h"

#include <stdexcept>

#include "SliceResourceContext.h"  // slice::MAX_POINTS

namespace lve
{
struct SliceContourPushConstants {
    glm::vec4 bounds;  // xMin, xMax, zMin, zMax
};

SliceContourRenderSystem::SliceContourRenderSystem(LveDevice& device,
                                                   VkRenderPass swapChainRenderPass)
    : m_lveDevice(device)
{
    CreatePipelineLayout();
    CreatePipeline(swapChainRenderPass);

    m_vertexBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        slice::MAX_POINTS,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_vertexBuffer->Map();
}

SliceContourRenderSystem::~SliceContourRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

void SliceContourRenderSystem::CreatePipelineLayout()
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SliceContourPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create contour pipeline layout!");
    }
}

void SliceContourRenderSystem::CreatePipeline(VkRenderPass renderPass)
{
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(glm::vec2);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDesc{};
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc.offset = 0;

    // 点管线：POINT_LIST
    {
        PipelineConfigInfo config{};
        LvePipeline::DefaultPipelineConfigInfo(config);
        config.renderPass = renderPass;
        config.pipelineLayout = m_pipelineLayout;
        config.bindingDescriptions = {bindingDesc};
        config.attributeDescriptions = {attrDesc};
        config.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        config.depthStencilInfo.depthTestEnable = VK_FALSE;
        config.depthStencilInfo.depthWriteEnable = VK_FALSE;
        config.colorBlendAttachment.blendEnable = VK_FALSE;

        m_pointPipeline = std::make_unique<LvePipeline>(
            m_lveDevice,
            "../../../res/shaders/spv/shader_slice_contour.vert.spv",
            "../../../res/shaders/spv/shader_slice_contour.frag.spv",
            config);
    }
}

void SliceContourRenderSystem::UpdatePoints(const std::vector<glm::vec2>& points)
{
    m_pointCount = static_cast<uint32_t>(points.size());
    if (m_pointCount == 0) {
        return;
    }
    if (m_pointCount > slice::MAX_POINTS) {
        m_pointCount = slice::MAX_POINTS;
    }

    m_vertexBuffer->WriteToBuffer(const_cast<glm::vec2*>(points.data()),
                                  m_pointCount * sizeof(glm::vec2));
}

void SliceContourRenderSystem::Render(VkCommandBuffer commandBuffer,
                                      const glm::vec4& bounds)
{
    if (m_pointCount == 0) {
        return;
    }

    VkBuffer vertexBuffers[] = {m_vertexBuffer->GetBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    SliceContourPushConstants push{};
    push.bounds = bounds;
    vkCmdPushConstants(commandBuffer,
                       m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(SliceContourPushConstants),
                       &push);

    // 画点
    m_pointPipeline->Bind(commandBuffer);
    vkCmdDraw(commandBuffer, m_pointCount, 1, 0, 0);
}

}  // namespace lve
