#pragma once

#include <memory>
#include <vector>
#include <glm.hpp>

#include "LveDevice.h"
#include "LvePipeline.h"

namespace lve
{
struct SliceDisplayPushConstants {
    int showMode = 1;      // 0: Solid, 1: Wireframe
    alignas(8) glm::vec2 texelSize;  // {1/w, 1/h}
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

private:
    void CreatePipelineLayout(VkDescriptorSetLayout displaySetLayout);
    void CreatePipeline(VkRenderPass renderPass);

private:
    LveDevice& m_lveDevice;
    std::unique_ptr<LvePipeline> m_lvePipeline;
    VkPipelineLayout m_pipelineLayout;
};
}  // namespace lve
