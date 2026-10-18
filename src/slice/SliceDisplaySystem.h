#pragma once

#include <memory>
#include <vector>
#include <glm.hpp>

#include "LveDevice.h"
#include "LvePipeline.h"

namespace lve
{
struct SliceDisplayPushConstants {
    int showMode = 1;              // 0: Solid, 1: Wireframe
    int gridEnabled = 1;           // 0: 关闭像素网格, 1: 开启
    float gridCellTexels = 1.0f;   // 每格多少个遮罩像素(texel), 默认 1
    float gridLineWidthPx = 1.0f;  // 网格线宽(屏幕像素)
    alignas(8) glm::vec2 texelSize;  // {1/w, 1/h}
    int rawContourEnabled = 1;     // 0: 关闭非亚像素黑色轮廓, 1: 开启
};

class SliceDisplaySystem
{
public:
    SliceDisplaySystem(LveDevice& device, VkRenderPass renderPass,
                       VkDescriptorSetLayout displaySetLayouts);
    ~SliceDisplaySystem();

    SliceDisplaySystem(const SliceDisplaySystem&) = delete;
    SliceDisplaySystem& operator=(const SliceDisplaySystem&) = delete;

    void Render(VkCommandBuffer commandBuffer, VkDescriptorSet displayDescriptorSet,
                uint32_t width, uint32_t height, bool isWireframe);

    // --- 像素网格 ---
    void SetGridEnabled(bool enabled)
    {
        m_gridEnabled = enabled;
    }
    bool IsGridEnabled() const
    {
        return m_gridEnabled;
    }
    // 每格多少个遮罩像素(texel); 1 = 每个像素一格
    void SetGridCellTexels(float texels)
    {
        m_gridCellTexels = texels;
    }
    void SetRawContourEnabled(bool enabled)
    {
        m_rawContourEnabled = enabled;
    }

private:
    void CreatePipelineLayout(VkDescriptorSetLayout displaySetLayout);
    void CreatePipeline(VkRenderPass renderPass);

private:
    LveDevice& m_lveDevice;
    std::unique_ptr<LvePipeline> m_lvePipeline;
    VkPipelineLayout m_pipelineLayout;

    bool m_gridEnabled = true;
    float m_gridCellTexels = 1.0f;
    float m_gridLineWidthPx = 1.0f;
    bool m_rawContourEnabled = true;
};
}  // namespace lve
