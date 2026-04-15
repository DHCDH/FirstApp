#include "RenderSystem.h"


#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>
#include <gtc/constants.hpp>

#include <stdexcept>
#include <array>
#include <iostream>
#include <unordered_map>
#include <map>

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

    CreatePipeline(renderPass);
    CreateTranslucentPipeline(renderPass);
    CreateInstancedPipeline(renderPass);
    CreateInvisibleInstancedPipeline(renderPass);
    CreateOutlinePipeline(renderPass);
}

void RenderSystem::CreatePipeline(VkRenderPass renderPass)
{
    // --- 主管线：填充模式 ---
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;

    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
    pipelineConfig.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
    pipelineConfig.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();

    std::cout << "-------" << "\n";
    for (size_t i = 0; i < pipelineConfig.bindingDescriptions.size(); ++i) {
        auto& bd = pipelineConfig.bindingDescriptions[i];
        std::cout << "  [" << i << "] binding=" << bd.binding
            << ", stride=" << bd.stride
            << ", rate=" << bd.inputRate << "\n";
    }

    m_lvePipeline = std::make_unique<LvePipeline>(m_lveDevice, "../../../res/shaders/spv/shader.vert.spv", "../../../res/shaders/spv/shader.frag.spv", pipelineConfig);
}

// --- 半透明管线 ---
void RenderSystem::CreateTranslucentPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo translucentConfig{};
    LvePipeline::DefaultPipelineConfigInfo(translucentConfig);
    translucentConfig.renderPass = renderPass;
    translucentConfig.pipelineLayout = m_pipelineLayout;

    // 开启Alpha混合，关闭深度写入
    LvePipeline::EnableAlphaBlending(translucentConfig);
    translucentConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;

    translucentConfig.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
    translucentConfig.attributeDescriptions =
        LveModel::Vertex::GetAttributeDescriptions();

    m_lvePipelineTranslucent =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/shader.vert.spv",
                                      "../../../res/shaders/spv/shader.frag.spv",
                                      translucentConfig);
}

void RenderSystem::CreateInstancedPipeline(VkRenderPass renderPass)
{
    // --- 副线：实例化模式 ---

    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);

    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;

    /*binding = 0*/
    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();

    /*binding = 1*/
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride =
        sizeof(glm::mat4);  // 第一个顶点(实例)读完之后，跳到下一个顶点(实例)所需跨字节数
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindingDescs.push_back(instanceBinding);

    /*把一个mat4拆成4个vec4，映射到顶点着色器location = 4, 5, 6, 7上*/
    uint32_t locBase = 4;
    for (uint32_t i = 0; i < locBase; ++i) {
        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 1;
        attribute.location = locBase + i;
        attribute.offset = sizeof(glm::vec4) * i;  // 在InstanceData中，这个attribute从offset处开始读数据
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;   // attribute从offset开始，从InstanceData中读取16个字节，当作4个float传递给shader中对应location
        attributeDescs.push_back(attribute);
    }
    pipelineConfig.bindingDescriptions = std::move(bindingDescs);
    pipelineConfig.attributeDescriptions = std::move(attributeDescs);

    std::cout <<  "[CreateInstancedPipeline] bindingDescriptions = " << pipelineConfig.bindingDescriptions.size() << "\n";
    for (size_t i = 0; i < pipelineConfig.bindingDescriptions.size(); i++) {
        auto& bd = pipelineConfig.bindingDescriptions[i];
        std::cout << "[" << i << "] binding = " << bd.binding << ", stride = " << bd.stride << ", rate = " << bd.inputRate << "\n";
    }

    m_lvePipelineInstanced = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_instanced.vert.spv",
        "../../../res/shaders/spv/shader.frag.spv",
        pipelineConfig
    );

}

void RenderSystem::CreateInvisibleInstancedPipeline(VkRenderPass renderPass)
{

    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);

    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;

    /*binding = 0*/
    auto bindingDescs = LveModel::Vertex::GetBindingDescriptions();
    auto attributeDescs = LveModel::Vertex::GetAttributeDescriptions();

    /*binding = 1*/
    VkVertexInputBindingDescription instanceBinding{};
    instanceBinding.binding = 1;
    instanceBinding.stride = sizeof(glm::mat4);  // 第一个顶点(实例)读完之后，跳到下一个顶点(实例)所需跨字节数
    instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    bindingDescs.push_back(instanceBinding);

    /*把一个mat4拆成4个vec4，映射到顶点着色器location = 4, 5, 6, 7上*/
    uint32_t locBase = 4;
    for (uint32_t i = 0; i < locBase; ++i) {
        VkVertexInputAttributeDescription attribute{};
        attribute.binding = 1;
        attribute.location = locBase + i;
        attribute.offset = sizeof(glm::vec4) * i;  // 在InstanceData中，这个attribute从offset处开始读数据
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;   // attribute从offset开始，从InstanceData中读取16个字节，当作4个float传递给shader中对应location
        attributeDescs.push_back(attribute);
    }
    pipelineConfig.bindingDescriptions = std::move(bindingDescs);
    pipelineConfig.attributeDescriptions = std::move(attributeDescs);

    pipelineConfig.colorBlendAttachment.colorWriteMask = 0; // 关闭颜色写入

    m_lvePipelineInstancedInvisible = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_instanced.vert.spv",
        "../../../res/shaders/spv/shader.frag.spv",
        pipelineConfig
    );
}

void RenderSystem::CreateOutlinePipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo outlineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(outlineConfig);
    outlineConfig.renderPass = renderPass;
    outlineConfig.pipelineLayout = m_pipelineLayout;

    // --- 核心设置 ---
    // 1. 深度测试开启，深度写入开启，防止轮廓透过前面的物体
    outlineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
    outlineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;

    // 2. 剔除正面 (Front Face Culling)！这是外扩法描边的灵魂
    outlineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    // 注意：如果你的模型原本法线是反的，这里可能要换成
    // VK_CULL_MODE_BACK_BIT，具体看运行效果

    // 3. 关闭颜色混合 (纯色覆盖)
    outlineConfig.colorBlendAttachment.blendEnable = VK_FALSE;

    outlineConfig.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
    outlineConfig.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();

    m_lvePipelineOutline =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/3dsimulation/shader_outline.vert.spv",
                                      "../../../res/shaders/spv/3dsimulation/shader_outline.frag.spv",
                                      outlineConfig);
}

/* 主循环中每帧都会调用renderGameObjects
 * 引用传递gameObjects，每次都会修改gameObjects中的数据并影响到下一个循环
 * gameObjects为FirstApp持有`
 * 
 */
void RenderSystem::RenderObjects(FrameInfo& frameInfo)
{
    // 原本使用无序的unordered_map，改为临时有序的map，保证透明度有效
    std::map<lve::LveObject::id_t, lve::LveObject*> sortedObjects;
    for (auto& kv : frameInfo.objects) {
        sortedObjects[kv.first] = &kv.second;
    }

    auto draw_single_object = [&](lve::LveObject* obj) {
        if (obj->model == nullptr) return;

        obj->model->Bind(frameInfo.commandBuffer);

        // ====== Submesh 分支（如果模型含有 submesh） ======
        const uint32_t subCount = obj->model->GetSubmeshCount();
        if (subCount > 0) {
            for (uint32_t si = 0; si < subCount; ++si) {
                // ---- 选 set=1：纹理 ----
                VkDescriptorSet set1 = frameInfo.dummyTexSet;
                if (frameInfo.submeshTexSets) {
                    auto it = frameInfo.submeshTexSets->find(obj->getId());
                    if (it != frameInfo.submeshTexSets->end() && si < it->second.size() &&
                        it->second[si] != VK_NULL_HANDLE) {
                        set1 = it->second[si];
                    }
                } else if (frameInfo.materialDescriptorSets) {
                    auto it = frameInfo.materialDescriptorSets->find(obj->getId());
                    if (it != frameInfo.materialDescriptorSets->end() &&
                        it->second != VK_NULL_HANDLE) {
                        set1 = it->second;
                    }
                }

                // ---- 选 set=2：材质 UBO（本帧）----
                VkDescriptorSet set2 = frameInfo.dummyMatSet;
                if (frameInfo.submeshMatSetThisFrame) {
                    auto it = frameInfo.submeshMatSetThisFrame->find(obj->getId());
                    if (it != frameInfo.submeshMatSetThisFrame->end() &&
                        si < it->second.size() && it->second[si] != VK_NULL_HANDLE) {
                        set2 = it->second[si];
                    }
                } else if (frameInfo.materialParamSets) {
                    auto it = frameInfo.materialParamSets->find(obj->getId());
                    if (it != frameInfo.materialParamSets->end() &&
                        it->second != VK_NULL_HANDLE) {
                        set2 = it->second;
                    }
                }

                // 一次性绑定 set=1/2，避免漏绑
                VkDescriptorSet sets12[2] = {set1, set2};
                vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelineLayout,
                                        /*firstSet=*/1,
                                        /*descriptorSetCount=*/2,
                                        sets12,
                                        0,
                                        nullptr);

                // push 常量（每个 submesh 同一变换，重复无害）
                SimplePushConstantData push{};
                push.modelMatrix = obj->transform.mat4();
                push.normalMatrix = obj->transform.normalMatrix();
                vkCmdPushConstants(
                    frameInfo.commandBuffer,
                    m_pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(SimplePushConstantData),
                    &push);

                // 绘制该 submesh
                obj->model->DrawSubmesh(frameInfo.commandBuffer, si);
            }
            return;
        }

        // ====== 无 submesh：沿用你原来的“每对象材质”路径 ======

        // 选 set=1（纹理）：原逻辑 + 占位兜底
        VkDescriptorSet set1 = frameInfo.dummyTexSet;
        if (frameInfo.materialDescriptorSets) {
            auto& mapTex = *frameInfo.materialDescriptorSets;
            auto itTex = mapTex.find(obj->getId());
            if (itTex != mapTex.end() && itTex->second != VK_NULL_HANDLE) {
                set1 = itTex->second;
            }
        }

        // 选 set=2（材质 UBO，本帧）：原逻辑 + 占位兜底
        VkDescriptorSet set2 = frameInfo.dummyMatSet;
        if (frameInfo.materialParamSets) {
            auto& mapMat = *frameInfo.materialParamSets;
            auto itMat = mapMat.find(obj->getId());
            if (itMat != mapMat.end() && itMat->second != VK_NULL_HANDLE) {
                set2 = itMat->second;
            }
        }

        // 一次性绑定 set=1/2，避免‘set(2) out of bounds’
        {
            VkDescriptorSet sets12[2] = {set1, set2};
            vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout,
                                    /*firstSet=*/1,
                                    /*descriptorSetCount=*/2,
                                    sets12,
                                    0,
                                    nullptr);
        }

        // push 常量 + 绘制整模型
        {
            SimplePushConstantData push{};
            push.modelMatrix = obj->transform.mat4();
            push.normalMatrix = obj->transform.normalMatrix();
            vkCmdPushConstants(frameInfo.commandBuffer,
                               m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(SimplePushConstantData),
                               &push);

            obj->model->Draw(frameInfo.commandBuffer);
        }
    };

    // --- 第一批次：渲染线框 ---
    m_lvePipelineOutline->Bind(frameInfo.commandBuffer);
    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &frameInfo.globalDescriptorSet,
                            0,
                            nullptr);

    for (auto& kv : frameInfo.objects) {
        if (kv.second.neededOutline) {
            // 只需要 push constants 和顶点，因为 outline shader 不需要贴图(set 1/2)
            SimplePushConstantData push{};
            push.modelMatrix = kv.second.transform.mat4();
            push.normalMatrix = kv.second.transform.normalMatrix();
            vkCmdPushConstants(frameInfo.commandBuffer,
                               m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(SimplePushConstantData),
                               &push);

            kv.second.model->Bind(frameInfo.commandBuffer);
            kv.second.model->Draw(frameInfo.commandBuffer);  // 简单绘制整个模型即可
        }
    }

    // --- 第二批次：渲染不透明物体 ---
    m_lvePipeline->Bind(frameInfo.commandBuffer);

    // 先绑定 set=0（全局 UBO），每帧一次
    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &frameInfo.globalDescriptorSet,
                            0,
                            nullptr);
    for (auto& kv : frameInfo.objects) {
        if (kv.second.transparency >= 1.) {
            draw_single_object(&kv.second);
        }
    }

    // --- 第三批次：渲染半透明物体 ---
    m_lvePipelineTranslucent->Bind(frameInfo.commandBuffer);
    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &frameInfo.globalDescriptorSet,
                            0,
                            nullptr);

    // 【注意】如果有多个半透明物体，理论上这里需要对 kv.second 按离相机的距离从远到近排序
    for (auto& kv : frameInfo.objects) {
        if (kv.second.transparency < 1.) {  // 判断为半透明物体
            draw_single_object(&kv.second);
        }
    }

}

void RenderSystem::RenderInstances(FrameInfo& frameInfo, const bool& shown)
{
    auto& cmd = frameInfo.commandBuffer;
    if(shown)
        m_lvePipelineInstanced->Bind(cmd);  // 绑定渲染管线
    else
        m_lvePipelineInstancedInvisible->Bind(cmd);

    vkCmdBindDescriptorSets(cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0,
        1,
        &frameInfo.globalDescriptorSet,
        0,
        nullptr);

    for (const auto& batch : frameInfo.instanceBatches) {

        if (!batch.model || batch.instanceBuffer == VK_NULL_HANDLE || batch.instanceCount == 0) {
            std::cout << "RenderSystem::RenderInstances: null model or instance buffer" << "\n";
            continue;
        }

        //std::cout << "batch: model: " << batch.model << "\nbuffer: " << batch.instanceBuffer << 
        //    "\nstride: " << batch.instanceStride << "\ncout: " << batch.instanceCount << "\n";

        batch.model->Bind(cmd);
        VkBuffer bufs[] = { batch.instanceBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 1, 1, bufs, offsets);

        VkDescriptorSet set1 = (batch.set1 != VK_NULL_HANDLE) ? batch.set1 : frameInfo.dummyTexSet;
        VkDescriptorSet set2 = (batch.set2 != VK_NULL_HANDLE) ? batch.set2 : frameInfo.dummyMatSet;
        VkDescriptorSet sets12[2] = { set1, set2 };

        vkCmdBindDescriptorSets(cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            1,
            2,
            sets12,
            0,
            nullptr);
        batch.model->DrawInstanced(cmd, batch.instanceCount);
    }
}


}