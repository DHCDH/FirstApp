#include "OptimizeMaskRenderSystem.h"

#include "OptimizeResourceContext.h"

using namespace lve;

namespace optimize
{
OptimizeMaskRenderSystem::OptimizeMaskRenderSystem(lve::LveDevice& device,
                                                   VkDescriptorSetLayout computeSetLayout)
    : m_lveDevice{device}
{
    CreateComputePipelineLayout(computeSetLayout);
    CreateComputePipelines();
}

void OptimizeMaskRenderSystem::CreateComputePipelineLayout(
    const VkDescriptorSetLayout& computeSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size =
        sizeof(PolarPushConstants);  // 我们在Shader中定义的Push结构体大小

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }
}

void OptimizeMaskRenderSystem::CreateComputePipelines()
{
    PipelineConfigInfo configInfo{};
    configInfo.pipelineLayout = m_computePipelineLayout;

    m_polarIntersectPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/pure_compute/"
        "shader_polar_intersect.comp.spv",  // 你的新shader路径
        configInfo);

    m_polarEvaluatePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/optimize/pure_compute/"
        "shader_polar_evaluate.comp.spv",  // 你的新评估shader
        configInfo);
}

void OptimizeMaskRenderSystem::ComputePolarIntersect(VkCommandBuffer cmd,
                                                     VkDescriptorSet descriptorSet,
                                                     const PolarPushConstants& push)
{
    m_polarIntersectPipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);
    vkCmdPushConstants(cmd,
                       m_computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(PolarPushConstants),
                       &push);

    // X轴处理三角形，Y轴处理步数，Z轴处理层数(并发位姿)
    uint32_t groupX = (push.numTriangles + 255) / 256;
    vkCmdDispatch(cmd, groupX, push.stepsPerPose, BATCH_LAYER_COUNT);
}

void OptimizeMaskRenderSystem::ComputePolarEvaluate(
    VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
    const PolarPushConstants& push)  // 加参数
{
    m_polarEvaluatePipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);

    // 增加这一行！把 push 常量传给 evaluate shader
    vkCmdPushConstants(cmd,
                       m_computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(PolarPushConstants),
                       &push);

    vkCmdDispatch(cmd, 1, 1, BATCH_LAYER_COUNT);
}

OptimizeMaskRenderSystem::~OptimizeMaskRenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_computePipelineLayout, nullptr);
}

}  // namespace optimize