#include "RenderSystem.h"


#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>
#include <gtc/constants.hpp>

#include <stdexcept>
#include <array>
#include <iostream>
#include <unordered_map>

namespace lve {

    struct SimplePushConstantData {
        glm::mat4 modelMatrix{ 1.f };   // 初始化为单位矩阵
        glm::mat4 normalMatrix{ 1.f };
    };

RenderSystem::RenderSystem(LveDevice& device, VkRenderPass renderPass, const std::vector<VkDescriptorSetLayout>& setLayouts)
    : m_lveDevice(device)
{
    CreatePipelineLayout(setLayouts); // 定义渲染管线的layout
    CreatePipelines(renderPass);
}

RenderSystem::~RenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

/* 创建渲染管线
 * 告诉vulkan渲染管线在执行时可以用哪些数据
 */
void RenderSystem::CreatePipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts)
{
    /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
     * 能被VS和FS两个阶段可见
     */
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = setLayouts;

    /*描述符集*/
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

void RenderSystem::CreatePipelines(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

    // --- 主管线：填充模式 ---
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;
    m_lvePipeline = std::make_unique<LvePipeline>(m_lveDevice, "../../../res/shaders/shader.vert.spv", "../../../res/shaders/shader.frag.spv", pipelineConfig);

}

/* 主循环中每帧都会调用renderGameObjects
 * 引用传递gameObjects，每次都会修改gameObjects中的数据并影响到下一个循环
 * gameObjects为FirstApp持有`
 * 
 */
#if 0
void RenderSystem::RenderObjects(FrameInfo& frameInfo)
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

        /*绑定材质纹理（set = 1）*/
        if (frameInfo.materialDescriptorSets) { // 可为空指针，便于兼容旧路径
          auto& mapTex = *frameInfo.materialDescriptorSets; // unordered_map<uint32_t, VkDescriptorSet>
          auto itTex = mapTex.find(obj.getId());
          if (itTex != mapTex.end()) {
            VkDescriptorSet matSet = itTex->second;
            vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 1, 1,
                                    &matSet, 0, nullptr);
          }
        }
        /*绑定材质参数UBO（set = 2）*/
        if (frameInfo.materialParamSets) {
            auto& mapMat = *frameInfo.materialParamSets;
            auto itMat = mapMat.find(obj.getId());
            if (itMat != mapMat.end()) {
                VkDescriptorSet matUboSet = itMat->second;
                vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 2, 1,
                                    &matUboSet, 0, nullptr);
            }
        }

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
#else
void RenderSystem::RenderObjects(FrameInfo& frameInfo)
{
    m_lvePipeline->Bind(frameInfo.commandBuffer);                                // [KEPT]

    // 先绑定 set=0（全局 UBO），每帧一次
    vkCmdBindDescriptorSets(frameInfo.commandBuffer,                              // [KEPT]
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0,
        1,
        &frameInfo.globalDescriptorSet,
        0,
        nullptr);

    for (auto& kv : frameInfo.objects) {                                          // [KEPT]
        auto& obj = kv.second;                                                    // [KEPT]
        if (obj.model == nullptr) continue;                                       // [KEPT]

        obj.model->Bind(frameInfo.commandBuffer);                                 // [NEW] 先绑定VB/IB

        // ====== Submesh 分支（如果模型含有 submesh） ======
        const uint32_t subCount = obj.model->GetSubmeshCount();                   // [NEW]
        if (subCount > 0) {                                                       // [NEW]
            for (uint32_t si = 0; si < subCount; ++si) {                          // [NEW]
                // ---- 选 set=1：纹理 ----
                VkDescriptorSet set1 = frameInfo.dummyTexSet;                     // [NEW] 默认占位
                if (frameInfo.submeshTexSets) {                                   // [NEW]
                    auto it = frameInfo.submeshTexSets->find(obj.getId());        // [NEW]
                    if (it != frameInfo.submeshTexSets->end()
                        && si < it->second.size()
                        && it->second[si] != VK_NULL_HANDLE) {
                        set1 = it->second[si];                                    // [NEW]
                    }
                }
                else if (frameInfo.materialDescriptorSets) {                    // [NEW] 退化到每-object
                    auto it = frameInfo.materialDescriptorSets->find(obj.getId());
                    if (it != frameInfo.materialDescriptorSets->end()
                        && it->second != VK_NULL_HANDLE) {
                        set1 = it->second;                                        // [NEW]
                    }
                }

                // ---- 选 set=2：材质 UBO（本帧）----
                VkDescriptorSet set2 = frameInfo.dummyMatSet;                     // [NEW] 默认占位
                if (frameInfo.submeshMatSetThisFrame) {                           // [NEW]
                    auto it = frameInfo.submeshMatSetThisFrame->find(obj.getId());
                    if (it != frameInfo.submeshMatSetThisFrame->end()
                        && si < it->second.size()
                        && it->second[si] != VK_NULL_HANDLE) {
                        set2 = it->second[si];                                    // [NEW]
                    }
                }
                else if (frameInfo.materialParamSets) {                         // [NEW] 退化到每-object
                    auto it = frameInfo.materialParamSets->find(obj.getId());
                    if (it != frameInfo.materialParamSets->end()
                        && it->second != VK_NULL_HANDLE) {
                        set2 = it->second;                                        // [NEW]
                    }
                }

                // 一次性绑定 set=1/2，避免漏绑
                VkDescriptorSet sets12[2] = { set1, set2 };                       // [NEW]
                vkCmdBindDescriptorSets(frameInfo.commandBuffer,                   // [NEW]
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_pipelineLayout,
                    /*firstSet=*/1,
                    /*descriptorSetCount=*/2,
                    sets12,
                    0, nullptr);

                // push 常量（每个 submesh 同一变换，重复无害）
                SimplePushConstantData push{};                                    // [NEW]
                push.modelMatrix = obj.transform.mat4();                         // [NEW]
                push.normalMatrix = obj.transform.normalMatrix();                 // [NEW]
                vkCmdPushConstants(frameInfo.commandBuffer, m_pipelineLayout,     // [NEW]
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(SimplePushConstantData), &push);

                // 绘制该 submesh
                obj.model->DrawSubmesh(frameInfo.commandBuffer, si);              // [NEW]
            }
            continue;                                                             // [NEW] 处理下一个对象
        }

        // ====== 无 submesh：沿用你原来的“每对象材质”路径 ======

        // 选 set=1（纹理）：原逻辑 + 占位兜底
        VkDescriptorSet set1 = frameInfo.dummyTexSet;                              // [NEW]
        if (frameInfo.materialDescriptorSets) {                                    // [CHG] 从“有则绑定”改为“选出一个要绑定的”
            auto& mapTex = *frameInfo.materialDescriptorSets;
            auto itTex = mapTex.find(obj.getId());
            if (itTex != mapTex.end() && itTex->second != VK_NULL_HANDLE) {
                set1 = itTex->second;                                                 // [CHG]
            }
        }

        // 选 set=2（材质 UBO，本帧）：原逻辑 + 占位兜底
        VkDescriptorSet set2 = frameInfo.dummyMatSet;                              // [NEW]
        if (frameInfo.materialParamSets) {                                         // [CHG] 从“有则绑定”改为“选出一个要绑定的”
            auto& mapMat = *frameInfo.materialParamSets;
            auto itMat = mapMat.find(obj.getId());
            if (itMat != mapMat.end() && itMat->second != VK_NULL_HANDLE) {
                set2 = itMat->second;                                             // [CHG]
            }
        }

        // 一次性绑定 set=1/2，避免‘set(2) out of bounds’
        {                                                                          // [NEW]
            VkDescriptorSet sets12[2] = { set1, set2 };                            // [NEW]
            vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout,
                /*firstSet=*/1,
                /*descriptorSetCount=*/2,
                sets12,
                0, nullptr);
        }

        // push 常量 + 绘制整模型
        {                                                                          // [KEPT]
            SimplePushConstantData push{};                                         // [KEPT]
            push.modelMatrix = obj.transform.mat4();                              // [KEPT]
            push.normalMatrix = obj.transform.normalMatrix();                      // [KEPT]
            vkCmdPushConstants(frameInfo.commandBuffer, m_pipelineLayout,          // [KEPT]
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->Draw(frameInfo.commandBuffer);                              // [KEPT]
        }
    }
}
#endif


}