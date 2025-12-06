#include "SliceMaskRenderSystem.h"


#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>
#include <gtc/constants.hpp>

#include <stdexcept>
#include <array>
#include <iostream>
#include <unordered_map>

namespace lve {

    struct SlicePushConstants {
        glm::mat4 modelMatrix;
        float yM;
        float thickness;
    };

    SliceMaskRenderSystem::SliceMaskRenderSystem(LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout setLayout)
        : m_lveDevice(device)
    {
        CreatePipelineLayout(setLayout); // 定义渲染管线的layout
        CreatePipelines(renderPass);
    }

    SliceMaskRenderSystem::~SliceMaskRenderSystem()
    {
        vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
    }

    /* 创建渲染管线
     * 告诉vulkan渲染管线在执行时可以用哪些数据
     */
    void SliceMaskRenderSystem::CreatePipelineLayout(const VkDescriptorSetLayout& setLayout)
    {
        /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
         * 能被VS和FS两个阶段可见
         */
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SlicePushConstants);

        /*描述符集*/
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;  // 描述符集布局数量（descriptor set layouts）
        pipelineLayoutInfo.pSetLayouts = &setLayout;   // 指向布局数组的指针
        /*把push constant范围装入管线布局*/
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        /*创建管线布局对象*/
        if (vkCreatePipelineLayout(m_lveDevice.device(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void SliceMaskRenderSystem::CreatePipelines(VkRenderPass renderPass)
    {
        assert(m_pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

        CreatePipeline(renderPass);
        CreateInstancedPipeline(renderPass);
    }

    void SliceMaskRenderSystem::CreatePipeline(VkRenderPass renderPass)
    {
        // --- 主管线：填充模式 ---
        PipelineConfigInfo pipelineConfig{};
        LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = m_pipelineLayout;
        pipelineConfig.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
        pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE; // 不剔除背面？
        pipelineConfig.colorBlendAttachment.blendEnable = VK_FALSE;

        pipelineConfig.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
        // pipelineConfig.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();

        auto allAttributes = LveModel::Vertex::GetAttributeDescriptions();
        pipelineConfig.attributeDescriptions.clear();
        pipelineConfig.attributeDescriptions.push_back(allAttributes[0]);

        m_lvePipeline = std::make_unique<LvePipeline>(m_lveDevice, 
            "../../../res/shaders/shader_slice_blank.vert.spv", 
            "../../../res/shaders/shader_slice_blank.frag.spv", 
            pipelineConfig);
    }

    void SliceMaskRenderSystem::CreateInstancedPipeline(VkRenderPass renderPass)
    {
        PipelineConfigInfo pipelineConfig{};
        LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = m_pipelineLayout;
        pipelineConfig.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
        pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        pipelineConfig.colorBlendAttachment.blendEnable = VK_FALSE;
        pipelineConfig.colorBlendInfo.logicOpEnable = VK_TRUE;  // 开启逻辑操作
        pipelineConfig.colorBlendInfo.logicOp = VK_LOGIC_OP_OR; // 按位或

        /*binding = 0: position only*/
        auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
        auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();

        std::vector<VkVertexInputAttributeDescription> posAttr;
        posAttr.push_back(attributeDescs[0]);
        attributeDescs = posAttr;

        /*binding = 1: instance matrix*/
        VkVertexInputBindingDescription instanceBinding{};
        instanceBinding.binding = 1;
        instanceBinding.stride = sizeof(glm::mat4);
        instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        bindingDescs.push_back(instanceBinding);

        /*location 4, 5, 6, 7*/
        for (uint32_t i = 0; i < 4; i++) {
            VkVertexInputAttributeDescription attribute{};
            attribute.binding = 1;
            attribute.location = i + 4;
            attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attribute.offset = sizeof(glm::vec4) * i;
            attributeDescs.push_back(attribute);
        }

        pipelineConfig.bindingDescriptions = bindingDescs;
        pipelineConfig.attributeDescriptions = attributeDescs;

        m_instancedPipeline = std::make_unique<LvePipeline>(m_lveDevice,
            "../../../res/shaders/shader_slice_instanced.vert.spv",
            "../../../res/shaders/shader_slice_instanced.frag.spv",
            pipelineConfig);
    }

    void SliceMaskRenderSystem::RenderBlank(const SliceInfo& sliceMaskInfo)
    {
        m_lvePipeline->Bind(sliceMaskInfo.commandBuffer);

        // 先绑定 set=0（全局 UBO），每帧一次
        vkCmdBindDescriptorSets(sliceMaskInfo.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            0,
            1,
            &sliceMaskInfo.globalDescriptorSet,
            0,
            nullptr);

        SlicePushConstants push{};
        push.modelMatrix = sliceMaskInfo.modelMatrix;
        push.yM = sliceMaskInfo.yM;
        push.thickness = sliceMaskInfo.thickness;

        vkCmdPushConstants(sliceMaskInfo.commandBuffer,
            m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(SlicePushConstants),
            &push);

        sliceMaskInfo.model.Bind(sliceMaskInfo.commandBuffer);
        sliceMaskInfo.model.Draw(sliceMaskInfo.commandBuffer);

    }

    void SliceMaskRenderSystem::RenderGrindingWheelInstances(const SliceInstancedInfo& info)
    {
        if (info.instanceCount == 0) {
            std::cout << "No grinding wheel instances to render!" << std::endl;
            return;
        }

        m_instancedPipeline->Bind(info.commandBuffer);

        vkCmdBindDescriptorSets(info.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            0,
            1,
            &info.globalDescriptorSet,
            0,
            nullptr);

        SlicePushConstants push{};
        push.modelMatrix = glm::mat4(1.0);  // 占位
        push.yM = info.yM;
        push.thickness = info.thickness;
        vkCmdPushConstants(info.commandBuffer,
            m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(SlicePushConstants),
            &push);

        /*binding = 0*/
        info.model.Bind(info.commandBuffer);

        /*binding = 1*/
        VkBuffer instanceBuffers[] = { info.instanceBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(info.commandBuffer,
            1,
            1,
            instanceBuffers,
            offsets);

        /*draw*/
        info.model.DrawInstanced(info.commandBuffer, info.instanceCount);
    }

}