#pragma once

#include <memory>
#include <vector>

#include <glm.hpp>

#include "LveBuffer.h"
#include "LveDevice.h"
#include "LvePipeline.h"

namespace lve
{
class SliceContourRenderSystem
{
public:
    SliceContourRenderSystem(LveDevice& device, VkRenderPass swapChainRenderPass);
    ~SliceContourRenderSystem();

    SliceContourRenderSystem(const SliceContourRenderSystem&) = delete;
    SliceContourRenderSystem& operator=(const SliceContourRenderSystem&) = delete;

    void UpdatePoints(const std::vector<glm::vec2>& points);
    void Render(VkCommandBuffer commandBuffer, const glm::vec4& bounds);

private:
    void CreatePipelineLayout();
    void CreatePipeline(VkRenderPass renderPass);

    LveDevice& m_lveDevice;
    VkPipelineLayout m_pipelineLayout;

    std::unique_ptr<LvePipeline> m_pointPipeline;

    std::unique_ptr<LveBuffer> m_vertexBuffer;
    uint32_t m_pointCount = 0;
};
}  // namespace lve
