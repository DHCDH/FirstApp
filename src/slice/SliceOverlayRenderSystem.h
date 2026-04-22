#pragma once

#include "LveDevice.h"
#include "LvePipeline.h"
#include "LveModel.h"
#include "../Global.h"

#include <memory>

namespace lve
{
class SliceOverlayRenderSystem
{
public:
    // 构造时，必须传入屏幕的 SwapChainRenderPass 和 相机的 DescriptorSetLayout
    SliceOverlayRenderSystem(LveDevice& device, VkRenderPass swapChainRenderPass,
                             VkDescriptorSetLayout globalSetLayout);
    ~SliceOverlayRenderSystem();

    SliceOverlayRenderSystem(const SliceOverlayRenderSystem&) = delete;
    SliceOverlayRenderSystem& operator=(const SliceOverlayRenderSystem&) = delete;

    // 渲染 3D 砂轮线框
    void RenderSliceContour(const SliceInstancedInfo& info);

private:
    void CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout);
    void CreatePipeline(VkRenderPass renderPass);

    LveDevice& m_lveDevice;
    VkPipelineLayout m_pipelineLayout;
    std::unique_ptr<LvePipeline> m_pipeline;
};
}