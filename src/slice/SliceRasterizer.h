#pragma once

#include <memory>
#include <vector>

#include "SliceResourceContext.h"
#include "systems/SliceMaskRenderSystem.h"

struct RasterizerData {
    lve::LveModel* blankModel = nullptr;
    lve::LveModel* grndWheelModel = nullptr;
    glm::mat4 blankMatrix{1.f};
    std::vector<glm::mat4> grndWheelInstances;
    glm::vec3 point{0.f, 0.f, 0.f};
    glm::vec3 normal{1.f, 0.f, 0.f};
};

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

    // 绘制线框
    void DrawOnscreenWireframe(VkCommandBuffer commandBuffer,
                               SliceResourceContext& context,
                               const RasterizerData& rasterizerData);

    // 绘制Mask
    void DrawMask(VkCommandBuffer commandBuffer, SliceResourceContext& context,
                  const RasterizerData& rasterizerData);

    // 执行计算
    void DispatchCompute(VkCommandBuffer commandBuffer, SliceResourceContext& context,
                         const SliceFrameData& frameData, const SliceViewConfig& viewConfig);

    VkBuffer GetGrndWheelInstancesBuffer() const
    {
        return m_grndWheelInstancesBuffer ? m_grndWheelInstancesBuffer->GetBuffer()
                                          : VK_NULL_HANDLE;
    }
    uint32_t GetGrndWheelInstancesCount() const
    {
        return m_grndWheelInstancesCount;
    }

private:
    lve::LveDevice& m_lveDevice;

    // 管线系统
    std::unique_ptr<lve::SliceMaskRenderSystem> m_renderSystem;

    // 实例缓冲区
    std::unique_ptr<lve::LveBuffer> m_grndWheelInstancesBuffer;
    uint32_t m_grndWheelInstancesCount{0};
};