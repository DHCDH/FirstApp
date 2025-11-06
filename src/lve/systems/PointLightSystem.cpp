#include "PointLightSystem.h"


#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>
#include <gtc/constants.hpp>

#include <stdexcept>
#include <array>
#include <iostream>

namespace lve {

struct PointLightPushConstants {
    glm::vec4 position{};
    glm::vec4 color{};
    float radius;
};

PointLightSystem::PointLightSystem(LveDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : m_lveDevice(device)
{
    CreatePipelineLayout(globalSetLayout); // 定义渲染管线的layout
    CreatePipelines(renderPass);
}

PointLightSystem::~PointLightSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

/* 创建渲染管线
 * 告诉vulkan渲染管线在执行时可以用哪些数据
 */
void PointLightSystem::CreatePipelineLayout(VkDescriptorSetLayout globalSetLayout)
{
    /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
     * 能被VS和FS两个阶段可见
     */
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PointLightPushConstants);

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

void PointLightSystem::CreatePipelines(VkRenderPass renderPass)
{
    std::cout << "PointLightSystem: CreatePipelines" << "\n";

    assert(m_pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    // --- 主管线：填充模式 ---
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.attributeDescriptions.clear();
    pipelineConfig.bindingDescriptions.clear();
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;
    m_lvePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/point_light.vert.spv",
        "../../../res/shaders/point_light.frag.spv",
        pipelineConfig);
}

void PointLightSystem::Update(FrameInfo& frameInfo, GlobalUbo& ubo)
{
    auto rotateLight = glm::rotate(glm::mat4(1.f), frameInfo.frameTime, {0.f, -1.f, 0.f});

    int lightIndex = 0;
    for (auto& kv : frameInfo.objects) {
        auto& obj = kv.second;
        if (obj.pointLight == nullptr) {
            //非点光源
            continue;
        }

        assert(lightIndex < MAX_LIGHTS && "Point lights exceed maximum specified");

        /*update light position*/
        obj.transform.translation = glm::vec3(rotateLight * glm::vec4(obj.transform.translation, 1.f));

        // copy light to ubo
        ubo.pointLights[lightIndex].position = glm::vec4(obj.transform.translation, 1.f);
        ubo.pointLights[lightIndex].color = glm::vec4(obj.color, obj.pointLight->lightIntensity);
        lightIndex += 1;
    }
    ubo.numLights = lightIndex;
}

/* 主循环中每帧都会调用renderGameObjects
 * 引用传递gameObjects，每次都会修改gameObjects中的数据并影响到下一个循环
 * gameObjects为FirstApp持有`
 * 
 */
void PointLightSystem::Render(FrameInfo& frameInfo)
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
        if (obj.pointLight == nullptr) continue;

        PointLightPushConstants push{};
        push.position = glm::vec4(obj.transform.translation, 1.f);
        push.color = glm::vec4(obj.color, obj.pointLight->lightIntensity);
        push.radius = obj.transform.scale.x;

        vkCmdPushConstants(
            frameInfo.commandBuffer,
            m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PointLightPushConstants),
            &push
        );
        vkCmdDraw(frameInfo.commandBuffer, 6, 1, 0, 0);
    }
}

}