#pragma once

#include "LvePipeline.h"
#include "LveDevice.h"

#include <memory>
#include <vector>

namespace lve
{
class SliceDisplaySystem
{
public:
    SliceDisplaySystem(LveDevice& device, VkRenderPass renderPass,
                       VkDescriptorSetLayout displaySetLayouts);
    ~SliceDisplaySystem();

    SliceDisplaySystem(const SliceDisplaySystem&) = delete;
    SliceDisplaySystem& operator=(const SliceDisplaySystem&) = delete;

    void Render(VkCommandBuffer commandBuffer, VkDescriptorSet displayDescriptorSet);

private:
    void CreatePipelineLayout(VkDescriptorSetLayout displaySetLayout);
    void CreatePipeline(VkRenderPass renderPass);

private:
    LveDevice& m_lveDevice;
    std::unique_ptr<LvePipeline> m_lvePipeline;
    VkPipelineLayout m_pipelineLayout;
};
}

