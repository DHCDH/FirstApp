#include "RenderSystem.h"

#define GLM_FORCE_RADIANS  // 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  //深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <array>
#include <glm.hpp>
#include <gtc/constants.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace lve
{
struct SimplePushConstantData {
    glm::mat4 modelMatrix{1.f};  // 初始化为单位矩阵
    glm::mat4 normalMatrix{1.f};
};

RenderSystem::RenderSystem(LveDevice& device, VkRenderPass renderPass,
                           const std::vector<VkDescriptorSetLayout>& setLayouts)
    : m_lveDevice(device)
{
    CreatePipelineLayout(setLayouts);  // 定义渲染管线的layout
    CreatePipelines(renderPass);
}

RenderSystem::~RenderSystem()
{
    vkDestroyPipelineLayout(m_lveDevice.device(), m_pipelineLayout, nullptr);
}

/* 创建渲染管线
 * 告诉vulkan渲染管线在执行时可以用哪些数据
 */
void RenderSystem::CreatePipelineLayout(
    const std::vector<VkDescriptorSetLayout>& setLayouts)
{
    /* 从偏移0开始，大小为sizeof(SimplePushConstantData)的一段push常量
     * 能被VS和FS两个阶段可见
     */
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = setLayouts;

    /*描述符集*/
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(
        descriptorSetLayouts.size());  // 描述符集布局数量（descriptor set layouts）
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();  // 指向布局数组的指针
    /*把push constant范围装入管线布局*/
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    /*创建管线布局对象*/
    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void RenderSystem::CreatePipelines(VkRenderPass renderPass)
{
    assert(m_pipelineLayout != nullptr &&
           "Cannot create pipeline before pipeline layout");

    CreatePipeline(renderPass);
    CreateTranslucentPipeline(renderPass);
    CreateInstancedPipeline(renderPass);
    CreateInvisibleInstancedPipeline(renderPass);
    CreateOutlinePipeline(renderPass);
    CreateThicknessMapPipeline(renderPass);
    CreateFullscreenPipeline(renderPass);
}

void RenderSystem::CreatePipeline(VkRenderPass renderPass)
{
    // --- 主管线：填充模式 ---
    PipelineConfigInfo pipelineConfig{};
    LvePipeline::DefaultPipelineConfigInfo(pipelineConfig);
    pipelineConfig.renderPass = renderPass;
    pipelineConfig.pipelineLayout = m_pipelineLayout;

    // 自定义视点模式，临时关闭普通背面剔除
    pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;

    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
    pipelineConfig.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
    pipelineConfig.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();

    std::cout << "-------"
              << "\n";
    for (size_t i = 0; i < pipelineConfig.bindingDescriptions.size(); ++i) {
        auto& bd = pipelineConfig.bindingDescriptions[i];
        std::cout << "  [" << i << "] binding=" << bd.binding << ", stride=" << bd.stride
                  << ", rate=" << bd.inputRate << "\n";
    }

    m_lvePipeline =
        std::make_unique<LvePipeline>(m_lveDevice,
                                      "../../../res/shaders/spv/shader.vert.spv",
                                      "../../../res/shaders/spv/shader.frag.spv",
                                      pipelineConfig);
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

    // 开启Alpha混合，开启深度写入
    LvePipeline::EnableAlphaBlending(pipelineConfig);
    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;

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
        attribute.offset =
            sizeof(glm::vec4) * i;  // 在InstanceData中，这个attribute从offset处开始读数据
        attribute.format =
            VK_FORMAT_R32G32B32A32_SFLOAT;  // attribute从offset开始，从InstanceData中读取16个字节，当作4个float传递给shader中对应location
        attributeDescs.push_back(attribute);
    }
    pipelineConfig.bindingDescriptions = std::move(bindingDescs);
    pipelineConfig.attributeDescriptions = std::move(attributeDescs);

    std::cout << "[CreateInstancedPipeline] bindingDescriptions = "
              << pipelineConfig.bindingDescriptions.size() << "\n";
    for (size_t i = 0; i < pipelineConfig.bindingDescriptions.size(); i++) {
        auto& bd = pipelineConfig.bindingDescriptions[i];
        std::cout << "[" << i << "] binding = " << bd.binding
                  << ", stride = " << bd.stride << ", rate = " << bd.inputRate << "\n";
    }

    m_lvePipelineInstanced = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_instanced.vert.spv",
        "../../../res/shaders/spv/shader.frag.spv",
        pipelineConfig);
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
        attribute.offset =
            sizeof(glm::vec4) * i;  // 在InstanceData中，这个attribute从offset处开始读数据
        attribute.format =
            VK_FORMAT_R32G32B32A32_SFLOAT;  // attribute从offset开始，从InstanceData中读取16个字节，当作4个float传递给shader中对应location
        attributeDescs.push_back(attribute);
    }
    pipelineConfig.bindingDescriptions = std::move(bindingDescs);
    pipelineConfig.attributeDescriptions = std::move(attributeDescs);

    pipelineConfig.colorBlendAttachment.colorWriteMask = 0;  // 关闭颜色写入

    m_lvePipelineInstancedInvisible = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_instanced.vert.spv",
        "../../../res/shaders/spv/shader.frag.spv",
        pipelineConfig);
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

    m_lvePipelineOutline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/3dsimulation/shader_outline.vert.spv",
        "../../../res/shaders/spv/3dsimulation/shader_outline.frag.spv",
        outlineConfig);
}

void RenderSystem::CreateThicknessMapPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    // 【核心 1】：彻底关闭颜色写入！它现在是个幽灵管线
    config.colorBlendAttachment.colorWriteMask = 0;

    // 【核心 2】：正反面全进 Shader，关闭深度测试
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;

    // 【核心 3】：开启模板测试，让硬件帮我们算 Winding Number！
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;

    // 正面：模板值 +1
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.front.writeMask = 0xFF;

    // 背面：模板值 -1
    config.depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;
    config.depthStencilInfo.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
    config.depthStencilInfo.back.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.back.compareMask = 0xFF;
    config.depthStencilInfo.back.writeMask = 0xFF;

    config.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
    config.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();

    m_lvePipelineThickness = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader.vert.spv",
        "../../../res/shaders/spv/3dsimulation/shader_thickness.frag.spv",
        config);
}

void RenderSystem::CreateFullscreenPipeline(VkRenderPass renderPass)
{
    PipelineConfigInfo config{};
    LvePipeline::DefaultPipelineConfigInfo(config);
    config.renderPass = renderPass;
    config.pipelineLayout = m_pipelineLayout;

    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.depthStencilInfo.depthTestEnable = VK_FALSE;

    // 开启模板测试：只有当屏幕像素的模板值 = 设定的参考值时，才画颜色
    config.depthStencilInfo.stencilTestEnable = VK_TRUE;
    config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
    config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    config.depthStencilInfo.front.compareMask = 0xFF;
    config.depthStencilInfo.front.writeMask = 0x00;  // 只读不写
    config.depthStencilInfo.back = config.depthStencilInfo.front;

    // 允许我们在绘制时，动态修改寻找的“模板目标值”
    config.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
    config.dynamicStateInfo.pDynamicStates = config.dynamicStateEnables.data();
    config.dynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(config.dynamicStateEnables.size());

    // 全屏绘制不需要传递顶点模型
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();

    m_lvePipelineFullscreen = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/3dsimulation/shader_fullscreen.vert.spv",
        "../../../res/shaders/spv/3dsimulation/shader_fullscreen.frag.spv",
        config);
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

    VkDescriptorSet sets12[2] = {frameInfo.dummyTexSet, frameInfo.dummyMatSet};
    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            1,  // 从 Set 1 开始绑
                            2,  // 绑 2 个集
                            sets12,
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
    if (shown)
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
        if (!batch.model || batch.instanceBuffer == VK_NULL_HANDLE ||
            batch.instanceCount == 0) {
            std::cout << "RenderSystem::RenderInstances: null model or instance buffer"
                      << "\n";
            continue;
        }

        // std::cout << "batch: model: " << batch.model << "\nbuffer: " <<
        // batch.instanceBuffer <<
        //    "\nstride: " << batch.instanceStride << "\ncout: " << batch.instanceCount <<
        //    "\n";

        batch.model->Bind(cmd);
        VkBuffer bufs[] = {batch.instanceBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 1, 1, bufs, offsets);

        VkDescriptorSet set1 =
            (batch.set1 != VK_NULL_HANDLE) ? batch.set1 : frameInfo.dummyTexSet;
        VkDescriptorSet set2 =
            (batch.set2 != VK_NULL_HANDLE) ? batch.set2 : frameInfo.dummyMatSet;
        VkDescriptorSet sets12[2] = {set1, set2};

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

void RenderSystem::RenderThicknessMap(FrameInfo& frameInfo)
{
    // 手动强制清空本帧的模板缓冲为 0！
    VkClearAttachment clearAttachment{};
    clearAttachment.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    clearAttachment.clearValue.depthStencil = {1.0f, 0};  // 0 是模板的清零值

    VkClearRect clearRect{};
    clearRect.rect.offset = {0, 0};
    clearRect.rect.extent = {8192, 8192};  // 暴力覆盖最大可能的屏幕尺寸
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    // 在任何绘制开始前，直接将整个屏幕的模板值暴力抹平！
    vkCmdClearAttachments(frameInfo.commandBuffer, 1, &clearAttachment, 1, &clearRect);

    // =========================================================
    // 阶段 1：静默累加算总账 (只写 Stencil Buffer，不画颜色)
    // =========================================================
    m_lvePipelineThickness->Bind(frameInfo.commandBuffer);

    vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout,
                            0,
                            1,
                            &frameInfo.globalDescriptorSet,
                            0,
                            nullptr);

    for (auto& kv : frameInfo.objects) {
        if (kv.second.model == nullptr) continue;
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
        kv.second.model->Draw(frameInfo.commandBuffer);
    }

    // =========================================================
    // 阶段 2：读取总账，进行全屏上色！
    // =========================================================
    m_lvePipelineFullscreen->Bind(frameInfo.commandBuffer);

    // 绘制 正数区 (实体穿透区)：画不同深浅的 蓝色
    for (int i = 1; i <= 6; i++) {
        // 告诉 GPU：只在模板值为 i 的地方画画
        vkCmdSetStencilReference(frameInfo.commandBuffer,
                                 VK_STENCIL_FACE_FRONT_AND_BACK,
                                 i);

        float intensity = 0.4f + i * 0.1f;  // 数值越大，蓝色越亮
        SimplePushConstantData pushColor{};
        // 巧妙借用 push 结构体的第一行传递 vec4 颜色，避免管线结构报错
        pushColor.modelMatrix[0] =
            glm::vec4(0.0f, 0.2f * intensity, 1.0f * intensity, 1.0f);

        vkCmdPushConstants(frameInfo.commandBuffer,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(SimplePushConstantData),
                           &pushColor);
        vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);  // 发射全屏三角形
    }

    // 绘制 负数区 (剖面区)：画不同深浅的 灰色
    // Vulkan 的模板是 8位无符号整数：-1 就是 255，-2 就是 254
    for (int i = 1; i <= 6; i++) {
        uint32_t ref = 256 - i;
        vkCmdSetStencilReference(frameInfo.commandBuffer,
                                 VK_STENCIL_FACE_FRONT_AND_BACK,
                                 ref);

        float intensity = 0.2f + i * 0.15f;
        SimplePushConstantData pushColor{};
        pushColor.modelMatrix[0] = glm::vec4(intensity, intensity, intensity, 1.0f);

        vkCmdPushConstants(frameInfo.commandBuffer,
                           m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(SimplePushConstantData),
                           &pushColor);
        vkCmdDraw(frameInfo.commandBuffer, 3, 1, 0, 0);
    }
}

}  // namespace lve