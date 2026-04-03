#include "OptimizeResourceContext.h"

#include <stdexcept>

#include "../Global.h"
#include "ArcProjectionSolver.h"
#include "LveFrameInfo.h"

using namespace lve;

namespace optimize
{
OptimizeResourceContext::OptimizeResourceContext(LveDevice& lveDevice, uint32_t width,
                                                 uint32_t height)
    : m_lveDevice(lveDevice), m_width(width), m_height(height)
{
    // 纯 Compute 架构，不再需要 Image、RenderPass 和 Framebuffer
    CreateComputeResources();
}

void OptimizeResourceContext::CreateComputeResources()
{
    // Z-Map Buffer: 256层 * 3600个uint
    m_zMapBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                               sizeof(uint32_t),
                                               BATCH_LAYER_COUNT * ZMAP_RESOLUTION * 2,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 三角形 Buffer (假设砂轮最多 100,000 个三角形)
    // 注意: Triangle 结构体必须是 {vec4, vec4, vec4}，大小为 48 字节
    m_triangleBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        48,
        100000,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Pose SSBO (从 Optimizer 移到了这里集中管理)
    m_poseSSBOBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(PoseData),
        BATCH_LAYER_COUNT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_poseSSBOBuffer->Map();

    m_particleBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(Particle),
        SWARM_SIZE,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 结果 Buffer
    m_resultBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                                 sizeof(BestResultData),
                                                 BATCH_LAYER_COUNT,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 最优结果 Buffer
    m_bestResultSSBOBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(BestResultData),
        1,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // --- 创建全局统一的 Compute Descriptor Set ---
    m_contourComputeSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(0,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // Triangles
            .AddBinding(1,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // ZMap
            .AddBinding(2,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // Poses
            .AddBinding(3,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // Results
            .AddBinding(4,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // BestResult
            .AddBinding(5,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_COMPUTE_BIT)  // Particles
            .Build();

    m_computeDescriptorPool = LveDescriptorPool::Builder(m_lveDevice)
                                  .SetMaxSets(1)
                                  .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6)
                                  .Build();

    auto triInfo = m_triangleBuffer->DescriptorInfo();
    auto zMapInfo = m_zMapBuffer->DescriptorInfo();
    auto poseInfo = m_poseSSBOBuffer->DescriptorInfo();
    auto resultInfo = m_resultBuffer->DescriptorInfo();
    auto bestResultInfo = m_bestResultSSBOBuffer->DescriptorInfo();
    auto particleInfo = m_particleBuffer->DescriptorInfo();

    LveDescriptorWriter(*m_contourComputeSetLayout, *m_computeDescriptorPool)
        .WriteBuffer(0, &triInfo)
        .WriteBuffer(1, &zMapInfo)
        .WriteBuffer(2, &poseInfo)
        .WriteBuffer(3, &resultInfo)
        .WriteBuffer(4, &bestResultInfo)
        .WriteBuffer(5, &particleInfo)
        .Build(m_contourDescriptorSet);
}

void OptimizeResourceContext::Resize(uint32_t newWidth, uint32_t newHeight)
{
    // 纯计算管线与屏幕分辨率无关，无需做任何事
}

OptimizeResourceContext::~OptimizeResourceContext()
{
    vkDeviceWaitIdle(m_lveDevice.device());
}

}  // namespace optimize