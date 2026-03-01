#pragma once

#include <string>
#include <vector>

#include "LveDevice.h"

namespace lve
{
struct PipelineConfigInfo {
    PipelineConfigInfo() = default;
    PipelineConfigInfo(const PipelineConfigInfo&) = delete;
    PipelineConfigInfo& operator=(const PipelineConfigInfo&) = default;

    std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

    VkPipelineViewportStateCreateInfo viewportInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
    VkPipelineRasterizationStateCreateInfo rasterizationInfo;
    VkPipelineMultisampleStateCreateInfo multisampleInfo;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlendInfo;
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
    std::vector<VkDynamicState> dynamicStateEnables;
    VkPipelineDynamicStateCreateInfo dynamicStateInfo;
    VkPipelineLayout pipelineLayout = nullptr;
    VkRenderPass renderPass = nullptr;
    uint32_t subpass = 0;
};

class LvePipeline
{
public:
    // 图形管线
    LvePipeline(LveDevice& device, const std::string& vertFilepath,
                const std::string& fragFilepath, const PipelineConfigInfo& configInfo,
                const std::string& geomFilepath = "");
    // 计算管线
    LvePipeline(LveDevice& device, const std::string& computeFilepath,
                const PipelineConfigInfo& configInfo);

    ~LvePipeline();

    LvePipeline(const LvePipeline&) = delete;
    LvePipeline& operator=(const LvePipeline&) = delete;

    // 管线绑定
    void Bind(VkCommandBuffer commandBuffer,
              VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS);

    /*创建默认管道配置的公共函数*/
    static void DefaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
    static void EnableAlphaBlending(PipelineConfigInfo& configInfo);

private:
    static std::vector<char> ReadFile(const std::string& filepath);

    void CreateGraphicsPipeline(const std::string& vertFilepath,
                                const std::string& fragFilepath,
                                const PipelineConfigInfo& configInfo,
                                const std::string& geomFilepath = "");
    void CreateComputePipeline(const std::string& computeFilepath,
                               const PipelineConfigInfo& configInfo);

    void CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

    LveDevice& m_lveDevice;
    VkPipeline m_graphicsPipeline;      // Vulkan管道对象的句柄
    VkShaderModule m_vertShaderModule{VK_NULL_HANDLE};  // Vulkan着色器模块的句柄
    VkShaderModule m_fragShaderModule{VK_NULL_HANDLE};
    VkShaderModule m_geomShaderModule{VK_NULL_HANDLE};
};

}  // namespace lve