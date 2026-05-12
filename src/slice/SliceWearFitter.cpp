#include "SliceWearFitter.h"

#include "Logger.h"
#include "RenderDocHelper.h"
#include "vulkan/vulkan_core.h"

namespace slice
{
SliceWearFitter::SliceWearFitter(lve::LveDevice& device,
                                 SliceMaskRenderSystem& renderSystem,
                                 SliceResourceContext& context,
                                 SliceRasterizer& rasterizer)
    : m_lveDevice(device),
      m_renderSystem(renderSystem),
      m_context(context),
      m_rasterizer(rasterizer)
{
    InitThreads(m_activeThreadCount);
}

void SliceWearFitter::InitThreads(uint32_t threadCount)
{
    m_threadResources.resize(threadCount);

    uint32_t graphicsQueueFamilyIndex =
        m_lveDevice.findPhysicalQueueFamilies().graphicsFamily;

    for (uint32_t i = 0; i < threadCount; i++) {
        // --- 创建独立Pool ---
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;  // 使用修正后的索引
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(m_lveDevice.device(),
                                &poolInfo,
                                nullptr,
                                &m_threadResources[i].pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create thread-local command pool!");
        }

        // --- 创建二级CommandBuffer ---
        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = m_threadResources[i].pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_lveDevice.device(),
                                     &allocInfo,
                                     &m_threadResources[i].secondaryBuffer) !=
            VK_SUCCESS) {
            throw std::runtime_error(
                "failed to allocate thread-local secondary command buffer!");
        }
    }
}

// --- 将需要评估的圆角半径分发给各个线程 ---
void SliceWearFitter::RecordCandidateBatch(const std::vector<float>& candidates,
                                           const ParametricInstancedData& pData,
                                           VkDescriptorSet globalDescriptorSet,
                                           VkDescriptorSet sdfLossDescriptorSet)
{
    uint32_t numCandidates = candidates.size();
    uint32_t batchSize = (numCandidates + m_activeThreadCount - 1) / m_activeThreadCount;

    std::vector<std::thread> threads;

    // 二级缓冲继承信息
    VkCommandBufferInheritanceInfo inherit{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inherit.renderPass = m_context.m_maskRenderPass;
    inherit.framebuffer = m_context.m_contactMaskFramebuffer;  // 假设在交集层做拟合

    for (uint32_t t = 0; t < m_activeThreadCount; t++) {
        uint32_t start = t * batchSize;
        if (start >= numCandidates) {
            break;
        }
        uint32_t end = std::min(start + batchSize, numCandidates);

        threads.emplace_back(std::thread([this,
                                          t,
                                          start,
                                          end,
                                          globalDescriptorSet,
                                          sdfLossDescriptorSet,
                                          &candidates,
                                          &pData,
                                          &inherit]() {
            VkCommandBuffer cmd = m_threadResources[t].secondaryBuffer;
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            begin.pInheritanceInfo = &inherit;
            vkBeginCommandBuffer(cmd, &begin);

            // --- 重新设置视口和剪裁---
            VkViewport viewport{0.f,
                                0.f,
                                (float)m_context.m_width,
                                (float)m_context.m_height,
                                0.f,
                                1.f};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {m_context.m_width, m_context.m_height}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // --- 预绑定公共网格资源（所有候选半径共享一个unitGrid）---
            VkBuffer vBuffers[] = {pData.unitGridBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, pData.unitGridIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            // --- 绑定实例Buffer ---
            VkBuffer instBuffers[] = {pData.instanceBuffer};
            vkCmdBindVertexBuffers(cmd, 1, 1, instBuffers, offsets);

            // --- 开始循环录制 ---
            m_renderSystem.RecordGrindingWheelCornerRadiusCandiates(cmd,
                                                                    globalDescriptorSet,
                                                                    sdfLossDescriptorSet,
                                                                    pData,
                                                                    candidates,
                                                                    start,
                                                                    end);

            vkEndCommandBuffer(cmd);
        }));
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

float SliceWearFitter::FitGrindingWheelCornerRadius(
    VkCommandBuffer mainCmd, const FitConfig& config,
    ParametricInstancedData& parametricData, VkDescriptorSet globalDescriptorSet,
    const RasterizerData& rasterizerData, const SliceFrameData& frameData,
    const SliceViewConfig& viewConfig)
{
    float a = config.grMin;
    float b = config.grMax;
    const float ratio = 0.382f;

    float x1 = a + ratio * (b - a);
    float x2 = b - ratio * (b - a);

    // 假设 sdfLossDescriptorSet 已经在 Context 中准备好并由外界或 Context 传进来
    // 暂且从 Context 获取（你需要后续在 Context 中添加对应的 Get 接口）
    VkDescriptorSet sdfLossSet = m_context.m_sdfLossDescriptorSet;

    for (uint32_t iter = 0; iter < config.maxIterations; iter++) {
        if ((b - a) < config.tolerance) break;

        if (iter == 0) {
            RENDERDOC_START;
            INFO("RenderDoc Capture Started for Iteration 0");
        }

        VkCommandBuffer iterCmd = m_lveDevice.beginSingleTimeCommands();

        // 1. 重置 GPU 上的 Loss 存储缓冲区 (SSBO) 为 0
        vkCmdFillBuffer(iterCmd,
                        m_context.m_lossBuffer->GetBuffer(),
                        0,
                        VK_WHOLE_SIZE,
                        0);

        // 插入屏障，确保后续渲染前清零完成
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.buffer = m_context.m_lossBuffer->GetBuffer();
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(iterCmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &barrier,
                             0,
                             nullptr);

        // --- 评估第一个候选点 ---
        parametricData.candidatesRadius = x1;
        parametricData.grIndex = 0;
        m_rasterizer.ProcessAllPlanes(iterCmd,
                                      m_context,
                                      rasterizerData,
                                      frameData,
                                      viewConfig,
                                      true,
                                      parametricData);

        // --- 评估第二个候选点 ---
        parametricData.candidatesRadius = x2;
        parametricData.grIndex = 1;
        m_rasterizer.ProcessAllPlanes(iterCmd,
                                      m_context,
                                      rasterizerData,
                                      frameData,
                                      viewConfig,
                                      true,
                                      parametricData);

        m_lveDevice.endSingleTimeCommands(iterCmd);

        if (iter == 0) {
            RENDERDOC_END;
            INFO("RenderDoc Capture Ended for Iteration 0");
        }

        m_context.m_lossBuffer->Map();
        uint32_t* lossData = (uint32_t*)m_context.m_lossBuffer->GetMappedMemory();
        float fLoss1 = static_cast<float>(lossData[0]);
        float fLoss2 = static_cast<float>(lossData[1]);
        m_context.m_lossBuffer->Unmap();

        DEBUG("[Iter %d] x1: %f, x2: %f, Loss1: %f, Loss2: %f",
              iter,
              x1,
              x2,
              fLoss1,
              fLoss2);

        if (fLoss1 < fLoss2) {
            b = x2;
            x2 = x1;
            x1 = a + ratio * (b - a);
        } else {
            a = x1;
            x1 = x2;
            x2 = b - ratio * (b - a);
        }
    }

    return (a + b) * 0.5f;
}

SliceWearFitter::~SliceWearFitter()
{
}

}  // namespace slice
