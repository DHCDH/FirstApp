#include "SliceRasterizer.h"

#include <iostream>

using namespace lve;

SliceRasterizer::SliceRasterizer(LveDevice& lveDevice, SliceResourceContext& context)
    : m_lveDevice(lveDevice)
{
    // 初始化RenderSystem，负责离屏渲染
    m_renderSystem = std::make_unique<SliceMaskRenderSystem>(
        m_lveDevice,
        context.m_maskRenderPass,
        context.m_globalSetLayout->GetDescriptorSetLayout(),
        context.m_contourComputeSetLayout->GetDescriptorSetLayout());
}

void SliceRasterizer::UpdateInstances(const std::vector<glm::mat4>& instanceData)
{
    m_grndWheelInstancesCount = static_cast<uint32_t>(instanceData.size());
    if (m_grndWheelInstancesCount == 0) {
        std::cout << "Instance count: " << m_grndWheelInstancesCount << "\n";
        return;
    }

    VkDeviceSize bufferSize = sizeof(glm::mat4) * m_grndWheelInstancesCount;
    uint32_t instanceSize = sizeof(glm::mat4);

    if (m_grndWheelInstancesBuffer == VK_NULL_HANDLE ||
        m_grndWheelInstancesBuffer->GetInstanceCount() < m_grndWheelInstancesCount) {
        /*现有缓冲过小，重建缓冲*/
        m_grndWheelInstancesBuffer = std::make_unique<LveBuffer>(
            m_lveDevice,
            instanceSize,
            m_grndWheelInstancesCount,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    // --- 创建Staging Buffer ---
    lve::LveBuffer stagingBuffer(
        m_lveDevice,
        sizeof(glm::mat4),
        m_grndWheelInstancesCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)instanceData.data());
    stagingBuffer.Flush();

    // -- 录制拷贝命令 ---
    VkCommandBuffer copyCmd = m_lveDevice.beginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(copyCmd,
                    stagingBuffer.GetBuffer(),
                    m_grndWheelInstancesBuffer->GetBuffer(),
                    1,
                    &copyRegion);

    // --- 提交并等待 ---
    m_lveDevice.endSingleTimeCommands(copyCmd);
}

void SliceRasterizer::DrawOnscreenWireframe(VkCommandBuffer commandBuffer, SliceResourceContext& context,
    const RasterizerData& rasterizerData)
{

}

void SliceRasterizer::DrawMask(VkCommandBuffer commandBuffer,
                               SliceResourceContext& context,
                               const RasterizerData& rasterizerData)
{
    // --- 配置离屏Render Pass ---
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = context.m_maskRenderPass;
    renderPassInfo.framebuffer = context.m_blankMaskFramebuffer;
    // 设置渲染区域
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {context.m_width, context.m_height};

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0u;
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    // 开始离屏Pass
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 设置动态视口和裁剪
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(context.m_width);
    viewport.height = static_cast<float>(context.m_height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {context.m_width, context.m_height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (m_renderSystem) {
        SlicePlaneInfo planeInfo{commandBuffer,
                                 context.m_globalDescriptorSet,  // 传入全局 UBO 描述符
                                 rasterizerData.normal,
                                 rasterizerData.point};
        m_renderSystem->BindPlaneInjectionPipeline(commandBuffer);
        m_renderSystem->RenderPlaneInjection(planeInfo);
    }

    // --- 绘制棒料 ---
    if (rasterizerData.blankModel != nullptr) {
        SliceDrawInfo info{commandBuffer,
                           *rasterizerData.blankModel,
                           rasterizerData.blankMatrix,
                           context.m_globalDescriptorSet,
                           rasterizerData.normal,
                           rasterizerData.point};

        // 写入Stencil
        m_renderSystem->BindBlankStencilPipeline(commandBuffer);
        m_renderSystem->RenderBlank(info);

        m_renderSystem->BindBlankColorPipeline(commandBuffer);
        m_renderSystem->RenderBlank(info);
    }

    // --- 绘制砂轮实例 ---
    if (rasterizerData.grndWheelModel != nullptr && m_grndWheelInstancesCount > 0) {
        SliceInstancedInfo instInfo{commandBuffer,
                                    *rasterizerData.grndWheelModel,
                                    m_grndWheelInstancesBuffer->GetBuffer(),
                                    m_grndWheelInstancesCount,
                                    context.m_globalDescriptorSet,
                                    rasterizerData.normal,
                                    rasterizerData.point};

        const uint32_t BATCH_SIZE = 100;
        for (uint32_t i = 0; i < m_grndWheelInstancesCount; i += BATCH_SIZE) {
            // 计算当前批次大小
            uint32_t curCount = BATCH_SIZE < m_grndWheelInstancesCount - i
                                    ? BATCH_SIZE
                                    : m_grndWheelInstancesCount - i;
            instInfo.instanceCount = curCount;

            /*绘制砂轮前表面*/
            m_renderSystem->BindGrindingWheelStencilFrontPipeline(commandBuffer);
            m_renderSystem->RenderGrindingWheelInstances(instInfo, i);

            /*绘制砂轮后表面*/
            m_renderSystem->BindGrindingWheelStencilBackPipeline(commandBuffer);
            m_renderSystem->RenderGrindingWheelInstances(instInfo, i);

            m_renderSystem->BindStencilResolvePipeline(commandBuffer);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);

            m_renderSystem->BindStencilClearPipeline(commandBuffer);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }
    } else {
        throw std::runtime_error("Invalid grinding wheel instances data.");
    }

    // 结束RenderPass
    vkCmdEndRenderPass(commandBuffer);
}

void SliceRasterizer::DispatchCompute(VkCommandBuffer commandBuffer,
                                      SliceResourceContext& context,
                                      const SliceFrameData& frameData,
                                      const SliceViewConfig& viewConfig)
{
    // --- 设置内存屏障与计算着色器 ---
    std::vector<VkImageMemoryBarrier> barriers;

    VkImageMemoryBarrier stencilBarrier{};
    stencilBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    stencilBarrier.image = context.m_depthStencilImage;
    stencilBarrier.subresourceRange =
        {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    stencilBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    stencilBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    stencilBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    stencilBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers.push_back(stencilBarrier);

    if (context.m_blankMaskImage != VK_NULL_HANDLE) {
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.image = context.m_blankMaskImage;  // 这是 Attachment 0
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // RenderPass 结束时，由于 finalLayout 设置，它已经是 COLOR_ATTACHMENT_OPTIMAL
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Display Shader 需要 SHADER_READ_ONLY_OPTIMAL
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers.push_back(colorBarrier);
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    // 清空GPU侧计数器
    vkCmdFillBuffer(commandBuffer,
                    context.m_counterBuffer->GetBuffer(),
                    0,
                    sizeof(uint32_t),
                    0);

    // 重置GPU计算结果buffer
    ResultData resultData;
    vkCmdUpdateBuffer(commandBuffer,
                      context.m_resultBuffer->GetBuffer(),
                      0,
                      sizeof(ResultData),
                      &resultData);

    std::array<VkBufferMemoryBarrier, 2> bufferBarriers{};

    // 增加一个 Buffer Barrier，确保计数器清零完成后再开始计算
    VkBufferMemoryBarrier counterBarrier{};
    counterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    counterBarrier.size = sizeof(uint32_t);
    counterBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    counterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    counterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    counterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    counterBarrier.buffer = context.m_counterBuffer->GetBuffer();
    counterBarrier.offset = 0;
    counterBarrier.size = VK_WHOLE_SIZE;
    bufferBarriers[0] = counterBarrier;

    VkBufferMemoryBarrier resultBarrier{};
    resultBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    resultBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resultBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    resultBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resultBarrier.buffer =
        context.m_resultBuffer
            ->GetBuffer();  // 确保 m_minDistBuffer 已在 InitComputeResources 中创建！
    resultBarrier.offset = 0;
    resultBarrier.size = VK_WHOLE_SIZE;
    bufferBarriers[1] = resultBarrier;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         static_cast<uint32_t>(bufferBarriers.size()),
                         bufferBarriers.data(),
                         0,
                         nullptr);

    // 派发提取任务
    if (m_renderSystem) {
        float dx =
            (viewConfig.xMax - viewConfig.xMin) / static_cast<float>(viewConfig.nX);
        float dz =
            (viewConfig.zMin - viewConfig.zMax) / static_cast<float>(viewConfig.nZ);
        glm::vec4 mapInfo = {viewConfig.xMin, viewConfig.zMax, dx, dz};

        SliceComputeInfo info{commandBuffer,
                              context.m_contourDescriptorSet,
                              viewConfig.nX,
                              viewConfig.nZ,
                              MAX_POINTS,
                              frameData.normal,
                              frameData.point,
                              mapInfo};
        m_renderSystem->DispatchExtractContour(info);
    }

    VkBufferMemoryBarrier computeToTransferBarriers[3] = {};

    computeToTransferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    computeToTransferBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeToTransferBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    computeToTransferBarriers[0].buffer = context.m_counterBuffer->GetBuffer();
    computeToTransferBarriers[0].size = VK_WHOLE_SIZE;

    computeToTransferBarriers[1] = computeToTransferBarriers[0];
    computeToTransferBarriers[1].buffer = context.m_resultBuffer->GetBuffer();

    computeToTransferBarriers[2] = computeToTransferBarriers[0];
    computeToTransferBarriers[2].buffer = context.m_contourPointsBuffer->GetBuffer();

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         3,
                         computeToTransferBarriers,
                         0,
                         nullptr);
    
    VkBufferCopy copyResult{};
    copyResult.srcOffset = 0;
    copyResult.dstOffset = 0;
    copyResult.size = sizeof(ResultData);
    vkCmdCopyBuffer(commandBuffer,
                    context.m_resultBuffer->GetBuffer(),
                    context.m_readbackBuffer->GetBuffer(),
                    1,
                    &copyResult);

    VkBufferCopy copyCounter{};
    copyCounter.srcOffset = 0;
    copyCounter.dstOffset = sizeof(ResultData);
    copyCounter.size = sizeof(uint32_t);
    vkCmdCopyBuffer(commandBuffer,
                    context.m_counterBuffer->GetBuffer(),
                    context.m_readbackBuffer->GetBuffer(),
                    1,
                    &copyCounter);

    VkBufferCopy copyPoints{};
    copyPoints.srcOffset = 0;
    copyPoints.dstOffset = sizeof(ResultData) + sizeof(uint32_t);
    copyPoints.size = sizeof(glm::vec2) * MAX_POINTS;  // 假设 maxPoints 是 50000
    vkCmdCopyBuffer(commandBuffer,
                    context.m_contourPointsBuffer->GetBuffer(),
                    context.m_readbackBuffer->GetBuffer(),
                    1,
                    &copyPoints);

    VkBufferMemoryBarrier transferToHostBarrier{};
    transferToHostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    transferToHostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transferToHostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    transferToHostBarrier.buffer = context.m_readbackBuffer->GetBuffer();
    transferToHostBarrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &transferToHostBarrier,
                         0,
                         nullptr);

    return;
}