#pragma once

#include <vulkan/vulkan.h>

#include <glm.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "LveDevice.h"
#include "SliceMaskRenderSystem.h"
#include "SliceRasterizer.h"
#include "SliceResourceContext.h"

namespace slice
{
// GPU-side golden section search state (std140 layout)
struct alignas(16) GpuSearchState {
    float a;            // offset  0: lower bracket bound
    float b;            // offset  4: upper bracket bound
    float ratio;        // offset  8: golden ratio inverse (0.382)
    float tol;          // offset 12: convergence tolerance
    float x1;           // offset 16: candidate 0
    float x2;           // offset 20: candidate 1
    uint32_t iteration; // offset 24: current iteration
    uint32_t maxIters;  // offset 28: max iterations
    uint32_t converged; // offset 32: 0=running, 1=done
    uint32_t flags;     // offset 36: bit0=use SSBO in vertex shader
    float bestR;        // offset 40: final result
    float _pad;         // offset 44: pad to 48 bytes
};
static_assert(sizeof(GpuSearchState) == 48, "GpuSearchState must be 48 bytes for std140");

class SliceWearFitter
{
public:
    struct FitConfig {
        float grMin = 0.05f;
        float grMax = 1.f;
        float tolerance = 0.001f;
        uint32_t maxIterations = 20;
    };

    SliceWearFitter(lve::LveDevice& device, SliceMaskRenderSystem& renderSystem,
                    SliceResourceContext& context, SliceRasterizer& rasterizer);
    ~SliceWearFitter();

    SliceWearFitter(const SliceWearFitter&) = delete;
    SliceWearFitter& operator=(const SliceWearFitter&) = delete;

    // --- 执行拟合算法 ---
    float FitGrindingWheelCornerRadius(VkCommandBuffer mainCmd, const FitConfig& config,
                                       ParametricInstancedData& parametricData,
                                       VkDescriptorSet globalDescriptorSet,
                                       const RasterizerData& rasterizerData,
                                       const SliceFrameData& frameData,
                                       const SliceViewConfig& viewConfig);

private:
    struct ThreadResource {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer secondaryBuffer = VK_NULL_HANDLE;
    };

    // 初始化/销毁线程资源
    void InitThreads(uint32_t threadCount);
    void CleanupThreads();

    // 初始化GPU搜索状态
    void InitializeSearchState(const FitConfig& config);

    // --- 录制一批候选者的计算指令 ---
    void RecordCandidateBatch(const std::vector<float>& candidates,
                              const ParametricInstancedData& pData,
                              VkDescriptorSet globalDescriptorSet,
                              VkDescriptorSet sdfLossDescriptorSet);

private:
    lve::LveDevice& m_lveDevice;
    SliceMaskRenderSystem& m_renderSystem;
    SliceResourceContext& m_context;
    SliceRasterizer& m_rasterizer;

    std::vector<ThreadResource> m_threadResources;
    uint32_t m_activeThreadCount{std::max(1u, std::thread::hardware_concurrency() / 2)};
};

}  // namespace slice
