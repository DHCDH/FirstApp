#pragma once

#include <memory>
#include <vector>

#include "SliceMaskRenderSystem.h"
#include "SliceResourceContext.h"

namespace slice
{
// 离屏渲染/光栅化器
class SliceRasterizer
{
public:
    SliceRasterizer(lve::LveDevice& device, SliceResourceContext& context);
    ~SliceRasterizer() = default;

    SliceRasterizer(SliceResourceContext&) = delete;
    SliceRasterizer& operator=(const SliceRasterizer&) = delete;

public:
    // 更新实例数据
    void UpdateInstances(const std::vector<glm::mat4>& instanceData);

    void ProcessAllPlanes(VkCommandBuffer commandBuffer, SliceResourceContext& context,
                          const RasterizerData& rasterizerData,
                          const SliceFrameData& frameData,
                          const SliceViewConfig& viewConfig, bool isAnalysisRequested,
                          const ParametricInstancedData& parametricData);

    // 执行计算
    void DispatchCompute(VkCommandBuffer commandBuffer, SliceResourceContext& context,
                         const SliceFrameData& frameData,
                         const SliceViewConfig& viewConfig, uint32_t planeIndex,
                         bool isAnalysisRequested);

    void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout);

    VkBuffer GetGrndWheelInstancesBuffer() const
    {
        return m_grndWheelInstancesBuffer ? m_grndWheelInstancesBuffer->GetBuffer()
                                          : VK_NULL_HANDLE;
    }
    uint32_t GetGrndWheelInstancesCount() const
    {
        return m_grndWheelInstancesCount;
    }
    const std::vector<SliceViewConfig>& GetLastMicroConfigs() const
    {
        return m_lastMicroConfigs;
    }
    SliceMaskRenderSystem& GetRenderSystem() const
    {
        return *m_renderSystem;
    }

private:
    void ReadbackFromGPU(VkCommandBuffer commandBuffer, SliceResourceContext& context,
                         uint32_t numPlane);

    lve::LveDevice& m_lveDevice;

    // 管线系统
    std::unique_ptr<SliceMaskRenderSystem> m_renderSystem;

    // 实例缓冲区
    std::unique_ptr<lve::LveBuffer> m_grndWheelInstancesBuffer;
    uint32_t m_grndWheelInstancesCount{0};

    std::vector<SliceViewConfig> m_lastMicroConfigs;
};
}  // namespace slice