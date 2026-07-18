#include "SliceWearFitter.h"

#include <cmath>
#include <limits>

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
    state.x1 = config.grMin + 0.382f * (config.grMax - config.grMin);
    state.x2 = config.grMax - 0.382f * (config.grMax - config.grMin);

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
    lve::LveBuffer& searchStateBuf = m_context.GetSearchStateBuffer();
    lve::LveBuffer& lossBuf = *m_context.m_lossBuffer;

    GpuSearchState* state =
        reinterpret_cast<GpuSearchState*>(searchStateBuf.GetMappedMemory());

    // Map loss buffer for CPU readback (HOST_COHERENT — no flush needed)
    if (!lossBuf.GetMappedMemory()) {
        lossBuf.Map();
    }
    uint32_t* lossValues = reinterpret_cast<uint32_t*>(lossBuf.GetMappedMemory());

    // Helper: evaluate loss at a single point via GPU
    auto evalPoint = [&](float point) -> float {
        lossValues[0] = 0;
        state->x1 = point;
        VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();
        parametricData.candidateIndex = 0;
        m_rasterizer.ProcessAllPlanes(cmd, m_context, rasterizerData, frameData,
                                      viewConfig, true, searchStateSet, parametricData);
        m_lveDevice.endSingleTimeCommands(cmd);
        return static_cast<float>(lossValues[0]);
    };

    // 2. Brent's method: combines parabolic interpolation with golden-section fallback.
    //    Typically converges in 5-8 iterations for smooth unimodal functions.

    const float cgold = 0.381966011f;
    const float eps = std::numeric_limits<float>::epsilon();
    const float tol = config.tolerance;

    float a = config.grMin;                         // bracket left
    float b = config.grMax;                         // bracket right
    float x = a + cgold * (b - a);                  // best point (golden section initial)
    float fx = evalPoint(x);
    float w = x, v = x;                             // second- and third-best points
    float fw = fx, fv = fx;
    float d = 0.f;                                  // step to take
    float ePrev = 0.f;                              // previous step size

    for (uint32_t iter = 1; iter < config.maxIterations; iter++) {
        float xm = (a + b) * 0.5f;
        float tol1 = eps * std::abs(x) + tol / 3.f;
        float tol2 = 2.f * tol1;

        // Convergence: interval small enough
        if (std::abs(x - xm) <= tol2 - (b - a) * 0.5f) {
            state->bestR = x;
            state->converged = 1;
            DEBUG("Brent converged at iteration %d, bestR = %f", iter, x);
            break;
        }

        bool useGolden = true;
        float p = 0.f, q = 0.f, r = 0.f;

        // Try inverse parabolic interpolation
        if (std::abs(ePrev) > tol1) {
            r = (x - w) * (fx - fv);
            q = (x - v) * (fx - fw);
            p = (x - v) * q - (x - w) * r;
            q = 2.f * (q - r);
            if (q > 0.f) p = -p;
            else         q = -q;
            r = ePrev;
            ePrev = d;

            // Accept parabolic step only if it lies within the bracket and is shrinking
            if (std::abs(p) < std::abs(0.5f * q * r) &&
                p > q * (a - x) &&
                p < q * (b - x)) {
                d = p / q;
                float u = x + d;
                if (u - a < tol2 || b - u < tol2)
                    d = (xm < x) ? -tol1 : tol1;
                useGolden = false;
            }
        }

        // Golden-section fallback
        if (useGolden) {
            ePrev = (x < xm) ? b - x : a - x;
            d = cgold * ePrev;
        }

        // Take the step (clamp to at least tol1)
        float u;
        if (std::abs(d) >= tol1)
            u = x + d;
        else
            u = x + (d > 0.f ? tol1 : -tol1);

        float fu = evalPoint(u);

        INFO("Iteration %d: radius = %.6f, loss = %.0f (%.6f mm^2)", iter, u, fu, fu / 100000.0f);

        // Update a, b, x, w, v
        if (fu <= fx) {
            if (u < x) b = x;
            else       a = x;
            v = w;  fv = fw;
            w = x;  fw = fx;
            x = u;  fx = fu;
        } else {
            if (u < x) a = u;
            else       b = u;
            if (fu <= fw || w == x) {
                v = w;  fv = fw;
                w = u;  fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u;  fv = fu;
            }
        }
    }

    // Fallback: if not converged within maxIterations
    if (state->converged == 0) {
        state->bestR = x;
        state->converged = 1;
    }

    INFO("GPU golden section search completed: bestR = %f", state->bestR);
    return state->bestR;
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
