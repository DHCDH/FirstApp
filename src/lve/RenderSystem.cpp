#include "RenderSystem.h"


#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>
#include <gtc/constants.hpp>

#include <stdexcept>
#include <array>
#include <iostream>

namespace lve {

    struct SimplePushConstantData {
        glm::mat4 modelMatrix{ 1.f };   // 初始化为单位矩阵
        glm::mat4 normalMatrix{ 1.f };
    };

RenderSystem::RenderSystem(LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : m_lveDevice(device)
{
    CreatePipelineLayout(globalSetLayout); // 定义渲染管线的layout
    CreatePipelines(renderPass);
    CreateAxisVertices();
}

RenderSystem::~RenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

/* 创建渲染管线
 * 告诉vulkan渲染管线在执行时可以用哪些数据
 */
void RenderSystem::CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout)
{
    /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
     * 能被VS和FS两个阶段可见
     */
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    /*描述符集*/
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{ globalSetLayout };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());  // 描述符集布局数量（descriptor set layouts）
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();   // 指向布局数组的指针

    /*把push constant范围装入管线布局*/
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    /*创建管线布局对象*/
    if (vkCreatePipelineLayout(m_lveDevice.device(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

/* 创建图形渲染管线对象
    * 功能：使用默认管线配置，结合当前的Swap Chain、Render Pass和Pipeline Layout创建一个完整的图形渲染管线
    */
void RenderSystem::CreatePipeline(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    /*配置视口大小*/
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);  // SwapChain的width和height不一定与窗口的宽高相同
    /*指定渲染通道*/
    pipelineConfig.renderPass = renderPass;
    /*绑定管线布局（createPipelineLayout）*/
    pipelineConfig.pipelineLayout = m_pipelineLayout;
    /*创建图形渲染管线对象*/
    m_lvePipeline = std::make_unique<LvePipeline>(m_lveDevice, "../../../res/shaders/shader.vert.spv", "../../../res/shaders/shader.frag.spv", pipelineConfig);
}

void RenderSystem::CreatePipelines(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    // --- 主管线：填充模式 ---
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;
    m_lvePipeline = std::make_unique<LvePipeline>(m_lveDevice, "../../../res/shaders/shader.vert.spv", "../../../res/shaders/shader.frag.spv", pipelineConfig);


    // --- 坐标轴管线：线段模型 ---
    PipelineConfigInfo axisConfig{};
    LvePipeline::DefaultPipelineConfigInfo(axisConfig);
    axisConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    axisConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE; // 线框模式可选
    axisConfig.renderPass = renderPass;
    axisConfig.pipelineLayout = m_pipelineLayout;
    /*让axisPipeline永远在最前*/
    axisConfig.depthStencilInfo.depthTestEnable = VK_FALSE; // 关闭深度测试
    axisConfig.depthStencilInfo.depthWriteEnable = VK_FALSE; // 不写入深度缓冲
    m_axisPipeline = std::make_unique<LvePipeline>(m_lveDevice, "res/shaders/simple_shader.vert.spv", "res/shaders/simple_shader.frag.spv", axisConfig);
}

/* 主循环中每帧都会调用renderGameObjects
 * 引用传递gameObjects，每次都会修改gameObjects中的数据并影响到下一个循环
 * gameObjects为FirstApp持有`
 * 
 */
void RenderSystem::RenderGameObjects(FrameInfo& frameInfo)
{
    m_lvePipeline->Bind(frameInfo.commandBuffer);

    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0,
        1,
        &frameInfo.globalDescriptorSet,
        0,
        nullptr);

    for (auto& kv : frameInfo.objects) {
        auto& obj = kv.second;
        if (obj.model == nullptr) continue;
        SimplePushConstantData push{};
        push.modelMatrix = obj.transform.mat4();
        push.normalMatrix = obj.transform.normalMatrix();

        vkCmdPushConstants(frameInfo.commandBuffer, m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SimplePushConstantData), &push);
        obj.model->Bind(frameInfo.commandBuffer);
        obj.model->Draw(frameInfo.commandBuffer);
    }
}

/*固定在窗口左下角的小坐标系*/
void RenderSystem::RenderAxis(VkCommandBuffer commandBuffer, const LveCamera& camera, VkExtent2D extent)
{
#if 0
    // 小视口放左下角
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<float>(extent.height - 200.f);
    viewport.width = 200.f;
    viewport.height = 200.f;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    // 只保留相机水平旋转
    glm::mat4 view = camera.GetView();
    glm::vec3 forward = glm::normalize(glm::vec3(view[0][2], 0.0f, view[2][2]));
    float angle = atan2(forward.x, forward.z);
    glm::mat4 rotY = glm::rotate(glm::mat4(1.f), -angle, glm::vec3(0.f, 1.f, 0.f));

    glm::mat4 proj = glm::perspective(glm::radians(35.f), 1.f, 0.01f, 10.f);
    // proj[1][1] *= -1;
    glm::mat4 pv = proj * rotY;

    // 画坐标轴
    m_axisPipeline->Bind(commandBuffer);
    SimplePushConstantData push{};
    // push.color = { 1.f, 1.f, 1.f };
    push.transform = pv * glm::translate(glm::mat4{ 1.f }, { 0.f, 0.f, -2.5f })
        * glm::scale(glm::mat4{ 1.f }, glm::vec3{ 0.6f });
    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(SimplePushConstantData), &push);

    m_axisModel->Bind(commandBuffer);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    // 恢复主视口
    VkViewport mainViewport{};
    mainViewport.x = 0.0f;
    mainViewport.y = 0.0f;
    mainViewport.width = static_cast<float>(extent.width);
    mainViewport.height = static_cast<float>(extent.height);
    mainViewport.minDepth = 0.0f;
    mainViewport.maxDepth = 1.0f;
    VkRect2D mainScissor{ {0, 0}, extent };
    vkCmdSetViewport(commandBuffer, 0, 1, &mainViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &mainScissor);
#endif

}

void RenderSystem::CreateAxisVertices() 
{
    LveModel::Builder axisModelBuilder;
    std::vector<LveModel::Vertex> axisVertices = {
        {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, {{1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, // X 红
        {{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}}, // Y 绿
        {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}}, {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}}, // Z 蓝
    };
    axisModelBuilder.vertices = axisVertices;

    m_axisModel = std::make_unique<LveModel>(m_lveDevice, axisModelBuilder);
}

}