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
                                                                    m_context.GetSearchStateDescriptorSet(),
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

void SliceWearFitter::InitializeSearchState(const FitConfig& config)
{
    GpuSearchState state{};
    state.a = config.grMin;
    state.b = config.grMax;
    state.ratio = 0.382f;
    state.tol = config.tolerance;
    state.maxIters = config.maxIterations;
    state.iteration = 0;
    state.converged = 0;
    state.flags = 1;  // bit 0 = 1: vertex shader reads x1/x2 from SSBO
    state.bestR = 0.f;
    state.x1 = 0.f;
    state.x2 = 0.f;

    m_context.GetSearchStateBuffer().WriteToBuffer(&state, sizeof(GpuSearchState));
    m_context.GetSearchStateBuffer().Flush();
}

float SliceWearFitter::FitGrindingWheelCornerRadius(
    VkCommandBuffer mainCmd, const FitConfig& config,
    ParametricInstancedData& parametricData, VkDescriptorSet globalDescriptorSet,
    const RasterizerData& rasterizerData, const SliceFrameData& frameData,
    const SliceViewConfig& viewConfig)
{
    // 1. Initialize search state on GPU buffer
    InitializeSearchState(config);

    VkDescriptorSet searchStateSet = m_context.GetSearchStateDescriptorSet();
    VkDescriptorSet sdfLossSet = m_context.m_sdfLossDescriptorSet;

    // 2. Record all iterations into a single command buffer
    VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();

    for (uint32_t iter = 0; iter < config.maxIterations; iter++) {
        // a. Golden update: init x1/x2 on iter 0, or update bracket on iter > 0
        m_renderSystem.DispatchGoldenUpdate(cmd, searchStateSet, sdfLossSet);

        // Barrier: ensure searchState visible to vertex shader
        VkBufferMemoryBarrier searchStateBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        searchStateBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        searchStateBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        searchStateBarrier.buffer = m_context.GetSearchStateBuffer().GetBuffer();
        searchStateBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &searchStateBarrier,
                             0,
                             nullptr);

        // b. Evaluate candidate 0
        parametricData.candidateIndex = 0;
        m_rasterizer.ProcessAllPlanes(cmd,
                                      m_context,
                                      rasterizerData,
                                      frameData,
                                      viewConfig,
                                      true,
                                      searchStateSet,
                                      parametricData);

        // c. Evaluate candidate 1
        parametricData.candidateIndex = 1;
        m_rasterizer.ProcessAllPlanes(cmd,
                                      m_context,
                                      rasterizerData,
                                      frameData,
                                      viewConfig,
                                      true,
                                      searchStateSet,
                                      parametricData);

        // Barrier: ensure loss[0], loss[1] visible to golden update compute shader
        VkBufferMemoryBarrier lossBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        lossBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        lossBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        lossBarrier.buffer = m_context.m_lossBuffer->GetBuffer();
        lossBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &lossBarrier,
                             0,
                             nullptr);
    }

    // 3. Submit once and wait
    m_lveDevice.endSingleTimeCommands(cmd);

    // 4. Read back final bestR from GPU (buffer already mapped in CreateSearchStateResources)
    GpuSearchState* state =
        reinterpret_cast<GpuSearchState*>(m_context.GetSearchStateBuffer().GetMappedMemory());
    float bestR = state->bestR;

    INFO("GPU golden section search completed: bestR = %f", bestR);
    return bestR;
}

void SliceWearFitter::CleanupThreads()
{
    for (auto& res : m_threadResources) {
        if (res.secondaryBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_lveDevice.device(), res.pool, 1, &res.secondaryBuffer);
        }
        if (res.pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_lveDevice.device(), res.pool, nullptr);
        }
    }
    m_threadResources.clear();
}

SliceWearFitter::~SliceWearFitter()
{
    CleanupThreads();
}

}  // namespace slice
