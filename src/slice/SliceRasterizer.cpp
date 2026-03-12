#include "SliceRasterizer.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "LveCamera.h"

using namespace lve;

SliceRasterizer::SliceRasterizer(LveDevice& lveDevice, SliceResourceContext& context)
    : m_lveDevice(lveDevice)
{
    // 初始化RenderSystem，负责离屏渲染
    m_renderSystem = std::make_unique<SliceMaskRenderSystem>(
        m_lveDevice,
        context.m_maskRenderPass,
        context.m_globalSetLayout->GetDescriptorSetLayout(),
        context.m_contourComputeSetLayout->GetDescriptorSetLayout(),
        context.m_bboxComputeSetLayout->GetDescriptorSetLayout());
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
    uint32_t numPlanes = static_cast<uint32_t>(frameData.planes.size());
    if (numPlanes == 0) {
        throw std::runtime_error("No plane to process");
    }

    SliceMaskRenderPassData renderPassData{context.m_maskRenderPass,
                                           context.m_width,
                                           context.m_height,
                                           context.m_blankMaskFramebuffer,
                                           context.m_globalDescriptorSet,
                                           m_grndWheelInstancesBuffer.get(),
                                           m_grndWheelInstancesCount};

    // --- 为每个截面准备独立的相机配置数组
    std::vector<SliceViewConfig> updatedViewConfigs(numPlanes, viewConfig);
    std::vector<GlobalUbo> updatedUbos(numPlanes);

    if (!isAnalysisRequested) {
        if (m_renderSystem) {
            m_renderSystem->RenderMask(commandBuffer,
                                       renderPassData,
                                       rasterizerData,
                                       frameData.displayPlane);
        }

        // --- 插入图像内存屏障，手动转换图像布局 ---
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.image = context.m_blankMaskImage;
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // 渲染管线结束时是 Attachment 状态
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // 屏幕显示系统需要 Read Only 状态
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // 提交管线屏障
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &colorBarrier);

        return;
    }

    VkCommandBuffer scoutCmd = m_lveDevice.beginSingleTimeCommands();

    {
        GlobalUbo scoutBaseUbo;
        void* scoutUboMapped = context.m_cameraUboBuffer->GetMappedMemory();
        if (scoutUboMapped) std::memcpy(&scoutBaseUbo, scoutUboMapped, sizeof(GlobalUbo));

        // 强制覆盖当前的 UBO
        vkCmdUpdateBuffer(scoutCmd,
                          context.m_cameraUboBuffer->GetBuffer(),
                          0,
                          sizeof(GlobalUbo),
                          &scoutBaseUbo);

        // 屏障：确保覆盖完成后，后续的 RenderMask 才能读取
        VkBufferMemoryBarrier scoutUboBarrier{};
        scoutUboBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        scoutUboBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        scoutUboBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
        scoutUboBarrier.buffer = context.m_cameraUboBuffer->GetBuffer();
        scoutUboBarrier.size = sizeof(GlobalUbo);
        vkCmdPipelineBarrier(
            scoutCmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            1,
            &scoutUboBarrier,
            0,
            nullptr);
    }

    // --- 初始化所有BBox的极值 ---
    std::vector<BBoxData> initBBoxes(numPlanes, {0xFFFFFFFF, 0xFFFFFFFF, 0, 0});
    vkCmdUpdateBuffer(scoutCmd,
                      context.m_bboxBuffer->GetBuffer(),
                      0,
                      sizeof(BBoxData) * numPlanes,
                      initBBoxes.data());

    VkBufferMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    clearBarrier.buffer = context.m_bboxBuffer->GetBuffer();
    clearBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(scoutCmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &clearBarrier,
                         0,
                         nullptr);

    for (uint32_t i = 0; i < numPlanes; i++) {
        if (m_renderSystem) {
            m_renderSystem->RenderMask(scoutCmd,
                                       renderPassData,
                                       rasterizerData,
                                       frameData.planes[i]);
        }

        VkImageMemoryBarrier scoutImageBarrier{};
        scoutImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        scoutImageBarrier.image = context.m_blankMaskImage;
        scoutImageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        scoutImageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        scoutImageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        scoutImageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        scoutImageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(scoutCmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &scoutImageBarrier);

        if (m_renderSystem) {
            m_renderSystem->ComputeBBox(scoutCmd,
                                        context.GetBBoxDescriptorSet(),
                                        context.m_width,
                                        context.m_height,
                                        i);
        }

        // --- 内部循环屏障，防止多个截面的 Shader 读写踩踏 ---
        VkBufferMemoryBarrier loopBarrier{};
        loopBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        loopBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        loopBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        loopBarrier.buffer = context.m_bboxBuffer->GetBuffer();
        loopBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(scoutCmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &loopBarrier,
                             0,
                             nullptr);
    }

    // --- 将BBox数组拷贝回CPU ---
    VkBufferMemoryBarrier bboxToTransferBarrier{};
    bboxToTransferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bboxToTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bboxToTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bboxToTransferBarrier.buffer = context.m_bboxBuffer->GetBuffer();
    bboxToTransferBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(scoutCmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &bboxToTransferBarrier,
                         0,
                         nullptr);

    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(BBoxData) * numPlanes;
    vkCmdCopyBuffer(scoutCmd,
                    context.m_bboxBuffer->GetBuffer(),
                    context.m_bboxReadbackBuffer->GetBuffer(),
                    1,
                    &copyRegion);

    VkBufferMemoryBarrier bboxReadBarrier{};
    bboxReadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bboxReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bboxReadBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bboxReadBarrier.buffer = context.m_bboxReadbackBuffer->GetBuffer();
    bboxReadBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(scoutCmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &bboxReadBarrier,
                         0,
                         nullptr);

    // 强制暂停，等待缓冲区数据写入完成
    m_lveDevice.endSingleTimeCommands(scoutCmd);

    // --- CPU为每个截面单独配发相机 ---
    context.m_bboxReadbackBuffer->Map();
    void* mappedData = context.m_bboxReadbackBuffer->GetMappedMemory();
    std::vector<BBoxData> gpuBounds(numPlanes);
    std::memcpy(gpuBounds.data(), mappedData, sizeof(BBoxData) * numPlanes);
    context.m_bboxReadbackBuffer->Unmap();

    GlobalUbo baseUbo;
    void* uboMapped = context.m_cameraUboBuffer->GetMappedMemory();
    if (uboMapped) std::memcpy(&baseUbo, uboMapped, sizeof(GlobalUbo));

    float scout_dx =
        (viewConfig.xMax - viewConfig.xMin) / static_cast<float>(context.m_width);
    float scout_dz =
        (viewConfig.zMin - viewConfig.zMax) / static_cast<float>(context.m_height);

    bool hasAnyIntersection = false;
    SliceViewConfig firstValidMicroConfig = viewConfig;

    // --- 找出所有有交集的截面并计算bbox ---
    for (uint32_t i = 0; i < numPlanes; i++) {
        uint32_t minX = gpuBounds[i].minX;
        uint32_t maxX = gpuBounds[i].maxX;
        uint32_t minY = gpuBounds[i].minY;
        uint32_t maxY = gpuBounds[i].maxY;

        // --- 判断bbox是否无效 ---
        if (minX == 0xFFFFFFFF || maxX <= minX || maxY <= minY) {
            updatedViewConfigs[i].nX = 0;
            continue;
        }

        float worldMinX = viewConfig.xMin + gpuBounds[i].minX * scout_dx;
        float worldMaxX = viewConfig.xMin + gpuBounds[i].maxX * scout_dx;
        float worldMinZ = viewConfig.zMax + gpuBounds[i].minY * scout_dz;
        float worldMaxZ = viewConfig.zMax + gpuBounds[i].maxY * scout_dz;

        float centerX = (worldMinX + worldMaxX) * 0.5f;
        float centerZ = (worldMinZ + worldMaxZ) * 0.5f;
        float halfX = std::abs((worldMaxX - worldMinX)) * 0.5f;
        float halfZ = std::abs((worldMaxZ - worldMinZ)) * 0.5f;

        float maxHalf = std::max({halfX, halfZ, 1.f}) * 1.1f;  // 留10%边距

        float aspect = static_cast<float>(viewConfig.xMax - viewConfig.xMin) /
                       static_cast<float>(viewConfig.zMax - viewConfig.zMin);

        updatedViewConfigs[i] = viewConfig;
        updatedViewConfigs[i].xMin = centerX - maxHalf * aspect;
        updatedViewConfigs[i].xMax = centerX + maxHalf * aspect;
        updatedViewConfigs[i].zMin = centerZ - maxHalf;
        updatedViewConfigs[i].zMax = centerZ + maxHalf;

        if (!hasAnyIntersection) {
            hasAnyIntersection = true;
            firstValidMicroConfig = updatedViewConfigs[i];
        }
    }

    // --- 统一分配相机参数并生成最终投影矩阵 ---
    for (uint32_t i = 0; i < numPlanes; i++) {
        if (updatedViewConfigs[i].nX == 0) {
            if (hasAnyIntersection) {
                updatedViewConfigs[i] = firstValidMicroConfig;
                // 没有交集，直接使用默认视角
                //updatedViewConfigs[i] = viewConfig;
            } else {
                updatedViewConfigs[i] = viewConfig;
            }
        }

        // --- 重新生成当前截面的投影矩阵 ---
        LveCamera tempCamera;
        tempCamera.SetOrthographicProjection(updatedViewConfigs[i].xMin,
                                             updatedViewConfigs[i].xMax,
                                             updatedViewConfigs[i].zMax,
                                             updatedViewConfigs[i].zMin,
                                             -4000.f,
                                             4000.f);
        updatedUbos[i] = baseUbo;
        updatedUbos[i].projection = tempCamera.GetProjection();
    }

    m_lastMicroConfigs = updatedViewConfigs;

    if (isAnalysisRequested) {
        vkCmdFillBuffer(commandBuffer,
                        context.m_counterBuffer->GetBuffer(),
                        0,
                        sizeof(uint32_t),
                        0);
        VkBufferMemoryBarrier counterBarrier{};
        counterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        counterBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterBarrier.buffer = context.m_counterBuffer->GetBuffer();
        counterBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &counterBarrier,
                             0,
                             nullptr);
    }

    for (uint32_t i = 0; i < numPlanes; i++) {
        if (isAnalysisRequested) {
            if (i > 0) {
                // --- 等上一次的Vertex Shader读完老UBO再允许更新 ---
                VkBufferMemoryBarrier waitUboBarrier{};
                waitUboBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                waitUboBarrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
                waitUboBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                waitUboBarrier.buffer = context.m_cameraUboBuffer->GetBuffer();
                waitUboBarrier.size = sizeof(GlobalUbo);
                vkCmdPipelineBarrier(commandBuffer,
                                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0,
                                     0,
                                     nullptr,
                                     1,
                                     &waitUboBarrier,
                                     0,
                                     nullptr);
            }

            vkCmdUpdateBuffer(commandBuffer,
                              context.m_cameraUboBuffer->GetBuffer(),
                              0,
                              sizeof(GlobalUbo),
                              &updatedUbos[i]);

            // --- 等UBO更新完再允许开始画 ---
            VkBufferMemoryBarrier uboBarrier{};
            uboBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            uboBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            uboBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
            uboBarrier.buffer = context.m_cameraUboBuffer->GetBuffer();
            uboBarrier.size = sizeof(GlobalUbo);
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 1,
                                 &uboBarrier,
                                 0,
                                 nullptr);

            if (m_renderSystem) {
                m_renderSystem->RenderMask(commandBuffer,
                                           renderPassData,
                                           rasterizerData,
                                           frameData.planes[i]);
            }

            DispatchCompute(commandBuffer,
                            context,
                            frameData,
                            isAnalysisRequested ? updatedViewConfigs[i] : viewConfig,
                            i,
                            isAnalysisRequested);

            bool isLastPlane = (i == numPlanes - 1);
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
        }
    }

    if (isAnalysisRequested) {
        ReadbackFromGPU(commandBuffer, context, numPlanes);
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

    // 重置GPU计算结果buffer
    ResultData resultData{};
    resultData.coreRadiusSqBits = 0xFFFFFFFF;
    vkCmdUpdateBuffer(commandBuffer,
                      context.m_resultBuffer->GetBuffer(),
                      sizeof(ResultData) * planeIdx,  // 根据索引计算内存偏移
                      sizeof(ResultData),
                      &resultData);

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

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         1,
                         &resultBarrier,
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