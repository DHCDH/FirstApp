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

void SliceRasterizer::ProcessAllPlanes(VkCommandBuffer commandBuffer,
                                       SliceResourceContext& context,
                                       const RasterizerData& rasterizerData,
                                       const SliceFrameData& frameData,
                                       const SliceViewConfig& viewConfig,
                                       bool isAnalysisRequested)
{
    uint32_t numPlane = static_cast<uint32_t>(frameData.planes.size());
    if (numPlane == 0) {
        throw std::runtime_error("No plane to process");
    }

    SliceMaskRenderPassData renderPassData{context.m_maskRenderPass,
                                           context.m_width,
                                           context.m_height,
                                           context.m_blankMaskFramebuffer,
                                           context.m_globalDescriptorSet,
                                           m_grndWheelInstancesBuffer.get(),
                                           m_grndWheelInstancesCount};

    auto process_single_plane = [&](uint32_t planeIdx, bool isLastPlane) {
        const Plane& curPlane = frameData.planes[planeIdx];

        // --- 离屏渲染 ---
        if (m_renderSystem) {
            m_renderSystem->RenderMask(commandBuffer,
                                       renderPassData,
                                       rasterizerData,
                                       curPlane);
        }

        // --- 派发计算 ---
        DispatchCompute(commandBuffer, context, frameData, viewConfig, planeIdx, isAnalysisRequested);

        // --- 尾部安全屏障 ---
        if (!isLastPlane) {
            VkMemoryBarrier tailBarrier{};
            tailBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            tailBarrier.srcAccessMask =
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            tailBarrier.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                     VK_PIPELINE_STAGE_TRANSFER_BIT |
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0,
                                 1,
                                 &tailBarrier,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
        }
    };

    for (uint32_t i = 0; i < numPlane; i++) {
        process_single_plane(i, (i == numPlane - 1));
    }

    if (isAnalysisRequested) {
        ReadbackFromGPU(commandBuffer, context, numPlane);
    }
}

void SliceRasterizer::DispatchCompute(VkCommandBuffer commandBuffer,
                                      SliceResourceContext& context,
                                      const SliceFrameData& frameData,
                                      const SliceViewConfig& viewConfig,
                                      uint32_t planeIdx, bool isAnalysisRequested)
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

    if (!isAnalysisRequested) {
        return;
    }

    // 清空GPU侧计数器
    vkCmdFillBuffer(commandBuffer,
                    context.m_counterBuffer->GetBuffer(),
                    0,
                    sizeof(uint32_t),
                    0);

    // 重置GPU计算结果buffer
    ResultData resultData{};
    resultData.coreRadiusSqBits = 0xFFFFFFFF;
    vkCmdUpdateBuffer(commandBuffer,
                      context.m_resultBuffer->GetBuffer(),
                      sizeof(ResultData) * planeIdx,  // 根据索引计算内存偏移
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

        SliceComputeInfo computeInfo{context.m_contourDescriptorSet,
                                     viewConfig.nX,
                                     viewConfig.nZ,
                                     MAX_POINTS,
                                     planeIdx,
                                     frameData.planes[planeIdx].normal,
                                     frameData.planes[planeIdx].point,
                                     mapInfo};
        m_renderSystem->ComputeFlute(commandBuffer,
                                     computeInfo,
                                     context.m_tipInfoBuffer.get());
    }

    return;
}

void SliceRasterizer::ReadbackFromGPU(VkCommandBuffer commandBuffer,
                                      SliceResourceContext& context, uint32_t numPlane)
{
    VkBufferMemoryBarrier computeToTransferBarriers[3] = {};

    computeToTransferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    computeToTransferBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeToTransferBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    computeToTransferBarriers[0].buffer = context.m_counterBuffer->GetBuffer();
    computeToTransferBarriers[0].size = VK_WHOLE_SIZE;

    computeToTransferBarriers[1] = computeToTransferBarriers[0];
    computeToTransferBarriers[1].buffer = context.m_resultBuffer->GetBuffer();

    computeToTransferBarriers[2] = computeToTransferBarriers[0];
    computeToTransferBarriers[2].buffer = context.m_sortedPointsBuffer->GetBuffer();

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
    copyResult.size = sizeof(ResultData) * numPlane;
    vkCmdCopyBuffer(commandBuffer,
                    context.m_resultBuffer->GetBuffer(),
                    context.m_readbackBuffer->GetBuffer(),
                    1,
                    &copyResult);

    VkBufferCopy copyCounter{};
    copyCounter.srcOffset = 0;
    copyCounter.dstOffset = sizeof(ResultData) * MAX_PLANES;
    copyCounter.size = sizeof(uint32_t);
    vkCmdCopyBuffer(commandBuffer,
                    context.m_counterBuffer->GetBuffer(),
                    context.m_readbackBuffer->GetBuffer(),
                    1,
                    &copyCounter);

    VkBufferCopy copyPoints{};
    copyPoints.srcOffset = 0;
    copyPoints.dstOffset = sizeof(ResultData) * MAX_PLANES + sizeof(uint32_t);
    copyPoints.size = sizeof(glm::vec2) * MAX_POINTS;  // 假设 maxPoints 是 50000
    vkCmdCopyBuffer(commandBuffer,
                    context.m_sortedPointsBuffer->GetBuffer(),
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