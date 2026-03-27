#include "GrindingWheelPoseOptimizer.h"

#include "../Global.h"
#include "ArcProjectionSolver.h"
#include "RenderDocHelper.h"

#include <fstream>
#include <iomanip>

using namespace lve;

namespace optimize
{
GrindingWheelPoseOptimizer::GrindingWheelPoseOptimizer(lve::LveDevice& device,
                                                       lve::LveModel& blank,
                                                       lve::LveModel& grndWheel)
    : m_lveDevice(device), m_blank(blank), m_grndWheel(grndWheel)
{
    InitSSBOResources();
    CalculateTransformMatrixes();
}

void GrindingWheelPoseOptimizer::InitSSBOResources()
{
    uint32_t bufferSize = sizeof(glm::mat4) * BATCH_LAYER_COUNT;
    m_poseSSBOBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::mat4),
        BATCH_LAYER_COUNT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,  // 作为 SSBO 使用
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 映射到 CPU 内存，永不 Unmap，方便后续高速装填
    m_poseSSBOBuffer->Map();

    // 创建 DescriptorSet 布局 (对应 set = 1, binding = 1)
    m_SSBOSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(1,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();

    m_descriptorPool = LveDescriptorPool::Builder(m_lveDevice)
                           .SetMaxSets(1)
                           .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
                           .Build();

    // 3. 写入描述符
    auto bufferInfo = m_poseSSBOBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_SSBOSetLayout, *m_descriptorPool)
        .WriteBuffer(1, &bufferInfo)
        .Build(m_SSBODescriptorSet);
}

void GrindingWheelPoseOptimizer::CalculateTransformMatrixes()
{
    PROFILE_SCOPE("CalculateTransformMatrixes");

    auto integrator = std::make_unique<NumericalIntegrator>();
    integrator->SetStrategy(
        std::make_unique<AdaptiveSimpsonStrategy>(1e-10, 1e-10, 1e-2, 1e-12, 0.5));
    ArcProjectionSolver arcProjectionSolver;
    arcProjectionSolver.SetIntegrator(std::move(integrator))
        .SetCutterParameters(CutterParameters{})
        .SetGrindingWheelParameters(GrindingWheelParameters{});

    m_transformMatrixes = arcProjectionSolver.CalculateGrindingWheelPose();

    std::cout << "[Optimizer] Generated " << m_transformMatrixes.size() << " poses.\n";
}

CameraData GrindingWheelPoseOptimizer::CalculateMicroCamera(
    const BBoxData& bbox, const SliceViewConfig& macroConfig, uint32_t texWidth,
    uint32_t texHeight, const Plane& plane)
{
    CameraData cameraData;

    glm::vec3 up = glm::vec3{0.f, 1.f, 0.f};
    // 如果法线刚好也是 Y 轴，为了防止万向节死锁，换一个上向量
    if (std::abs(glm::dot(plane.normal, up)) > 0.999f) {
        up = glm::vec3(0.f, 0.f, 1.f);
    }
    glm::mat4 viewMatrix = glm::lookAt(plane.point + plane.normal, plane.point, up);

    if (bbox.minX == 0xFFFFFFFF || bbox.maxX <= bbox.minX || bbox.maxY <= bbox.minY) {
        // BBox无效，返回宏观相机
        LveCamera fallbackCam;
        fallbackCam.SetOrthographicProjection(macroConfig.xMin,
                                              macroConfig.xMax,
                                              macroConfig.zMax,
                                              macroConfig.zMin,
                                              -4000.f,
                                              4000.f);
        cameraData.projView = fallbackCam.GetProjection() * viewMatrix;
        cameraData.mapInfo = {macroConfig.xMin,
                              macroConfig.zMax,
                              (macroConfig.xMax - macroConfig.xMin) / texWidth,
                              (macroConfig.zMin - macroConfig.zMax) / texHeight};

        return cameraData;
    }

    // 2. 宏观像素 -> 宏观物理坐标
    float macroDx = (macroConfig.xMax - macroConfig.xMin) / static_cast<float>(texWidth);
    float macroDz = (macroConfig.zMin - macroConfig.zMax) / static_cast<float>(texHeight);

    float worldMinX = macroConfig.xMin + bbox.minX * macroDx;
    float worldMaxX = macroConfig.xMin + bbox.maxX * macroDx;
    float worldMinZ = macroConfig.zMax + bbox.minY * macroDz;
    float worldMaxZ = macroConfig.zMax + bbox.maxY * macroDz;

    // 3. 算出中心点和物理半径，并加上 1.2 倍的安全边缘缓冲
    float centerX = (worldMinX + worldMaxX) * 0.5f;
    float centerZ = (worldMinZ + worldMaxZ) * 0.5f;
    float halfX = std::abs(worldMaxX - worldMinX) * 0.5f;
    float halfZ = std::abs(worldMaxZ - worldMinZ) * 0.5f;

    float maxHalf = std::max({halfX, halfZ, 0.5f}) * 1.2f;
    float aspect = static_cast<float>(texWidth) / static_cast<float>(texHeight);

    float newXMin = centerX - maxHalf * aspect;
    float newXMax = centerX + maxHalf * aspect;
    float newZMin = centerZ - maxHalf;
    float newZMax = centerZ + maxHalf;

    // 4. 生成极限局部放大的 Projection 矩阵和 mapInfo
    LveCamera microCam;
    microCam
        .SetOrthographicProjection(newXMin, newXMax, newZMax, newZMin, -4000.f, 4000.f);

    cameraData.projView = microCam.GetProjection() * viewMatrix;
    cameraData.mapInfo = {newXMin,
                          newZMax,
                          (newXMax - newXMin) / texWidth,
                          (newZMin - newZMax) / texHeight};

    return cameraData;
}

void GrindingWheelPoseOptimizer::RunOptimization(
    OptimizeResourceContext& context, OptimizeMaskRenderSystem& maskRenderSystem,
    OptimizePoseRenderSystem& wheelRenderSystem, const SliceViewConfig& viewConfig,
    Plane plane, BatchedWheelPushConstants wheelPushData)
{
    PROFILE_SCOPE("Run Optimization");

    uint32_t totalPoses = static_cast<uint32_t>(m_transformMatrixes.size());
    if (totalPoses == 0) return;

    std::cout << "[Optimizer] Starting GPU evaluation loop for " << totalPoses
              << " poses..."
              << "\n ";

    // --- 映射读回内存 ---
    context.GetResultBuffer()->Map();
    ResultData* mappedResults = (ResultData*)context.GetResultBuffer()->GetMappedMemory();

    context.GetBBoxReadbackBuffer()->Map();
    BBoxData* mappedBBoxes =
        (BBoxData*)context.GetBBoxReadbackBuffer()->GetMappedMemory();

    for (uint32_t i = 0; i < totalPoses; i += BATCH_LAYER_COUNT) {
        std::cout << "\r[Optimizer] Processing batch: " << i << " / " << totalPoses
                  << " (" << (i * 100 / totalPoses) << "%) completed..." << std::flush;

        uint32_t curBatchSize = std::min((uint32_t)BATCH_LAYER_COUNT, totalPoses - i);

#ifdef BBOX
        // 将BATCH_LAYER_COUNT数量的位姿矩阵填进SSBO
        memcpy(m_poseSSBOBuffer->GetMappedMemory(),
               m_transformMatrixes.data() + i,
               curBatchSize * sizeof(glm::mat4));

        CameraData macroCamData = CalculateMicroCamera({0xFFFFFFFF, 0, 0, 0},
                                                       viewConfig,
                                                       context.GetWidth(),
                                                       context.GetHeight(), plane);
        std::vector<CameraData> macroCameras(BATCH_LAYER_COUNT, macroCamData);
        context.UpdateGlobalSSBO(macroCameras.data(),
                                 sizeof(CameraData) * BATCH_LAYER_COUNT);
        

        VkCommandBuffer cmdScout = m_lveDevice.beginSingleTimeCommands();

        // --- 清空BBox缓冲区 ---
        std::vector<BBoxData> initBBoxes(BATCH_LAYER_COUNT,
                                         {0xFFFFFFFF, 0xFFFFFFFF, 0, 0});
        vkCmdUpdateBuffer(cmdScout,
                          context.GetBBoxBuffer()->GetBuffer(),
                          0,
                          sizeof(BBoxData) * BATCH_LAYER_COUNT,
                          initBBoxes.data());

        VkBufferMemoryBarrier bboxClearBarrier{};
        bboxClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bboxClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bboxClearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bboxClearBarrier.buffer = context.GetBBoxBuffer()->GetBuffer();
        bboxClearBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmdScout,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &bboxClearBarrier,
                             0,
                             nullptr);

        // --- 画图 ---
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = context.GetMaskRenderPass();
        renderPassInfo.framebuffer = context.GetBlankMaskFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {context.GetWidth(), context.GetHeight()};
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color.uint32[0] = 0u;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmdScout, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0.0f,
                            0.0f,
                            (float)context.GetWidth(),
                            (float)context.GetHeight(),
                            0.f,
                            1.f};
        vkCmdSetViewport(cmdScout, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {context.GetWidth(), context.GetHeight()}};
        vkCmdSetScissor(cmdScout, 0, 1, &scissor);

        OptimizePlaneInfo planeInfo{cmdScout,
                                    context.GetGlobalDescriptorSet(),
                                    plane.normal,
                                    plane.point};
        maskRenderSystem.BindPlaneInjectionPipeline(cmdScout);
        maskRenderSystem.RenderPlaneInjection(planeInfo);

        OptimizeDrawInfo blankInfo{cmdScout,
                                   m_blank,
                                   glm::mat4(1.0f),
                                   context.GetGlobalDescriptorSet(),
                                   plane.normal,
                                   plane.point};
        maskRenderSystem.BindBlankStencilPipeline(cmdScout);
        maskRenderSystem.RenderBlank(blankInfo);
        maskRenderSystem.BindBlankColorPipeline(cmdScout);
        maskRenderSystem.RenderBlank(blankInfo);

        wheelRenderSystem.RenderBatchWheels(cmdScout,
                                            wheelPushData,
                                            curBatchSize,
                                            context.GetGlobalDescriptorSet(),
                                            m_SSBODescriptorSet);
        vkCmdEndRenderPass(cmdScout);

        // --- 读回BBox ---
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.image = context.GetBlankMaskImage();
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,
                                         0,
                                         1,
                                         0,
                                         BATCH_LAYER_COUNT};
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmdScout,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &colorBarrier);

        maskRenderSystem.ComputeBBox(cmdScout,
                                     context.GetBBoxDescriptorSet(),
                                     context.GetWidth(),
                                     context.GetHeight());

        VkBufferMemoryBarrier bboxSyncBarrier{};
        bboxSyncBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bboxSyncBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bboxSyncBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bboxSyncBarrier.buffer = context.GetBBoxBuffer()->GetBuffer();
        bboxSyncBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmdScout,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &bboxSyncBarrier,
                             0,
                             nullptr);

        VkBufferCopy copyBBox{};
        copyBBox.srcOffset = 0;
        copyBBox.dstOffset = 0;
        copyBBox.size = sizeof(BBoxData) * BATCH_LAYER_COUNT;
        vkCmdCopyBuffer(cmdScout,
                        context.GetBBoxBuffer()->GetBuffer(),
                        context.GetBBoxReadbackBuffer()->GetBuffer(),
                        1,
                        &copyBBox);

        // 阻塞等待
        m_lveDevice.endSingleTimeCommands(cmdScout);
        

        // --- 根据BBox计算MicroCamera ---
        std::vector<CameraData> microCameras(BATCH_LAYER_COUNT);
        for (uint32_t j = 0; j < curBatchSize; ++j) {
            microCameras[j] = CalculateMicroCamera(mappedBBoxes[j],
                                                   viewConfig,
                                                   context.GetWidth(),
                                                   context.GetHeight(), plane);
        }
        context.UpdateGlobalSSBO(microCameras.data(),
                                 sizeof(CameraData) * BATCH_LAYER_COUNT);

        VkCommandBuffer cmdPrecise = m_lveDevice.beginSingleTimeCommands();

        vkCmdBeginRenderPass(cmdPrecise, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmdPrecise, 0, 1, &viewport);
        vkCmdSetScissor(cmdPrecise, 0, 1, &scissor);

        planeInfo.commandBuffer = cmdPrecise;
        blankInfo.commandBuffer = cmdPrecise;

        maskRenderSystem.BindPlaneInjectionPipeline(cmdPrecise);
        maskRenderSystem.RenderPlaneInjection(planeInfo);

        maskRenderSystem.BindBlankStencilPipeline(cmdPrecise);
        maskRenderSystem.RenderBlank(blankInfo);
        maskRenderSystem.BindBlankColorPipeline(cmdPrecise);
        maskRenderSystem.RenderBlank(blankInfo);

        wheelRenderSystem.RenderBatchWheels(cmdPrecise,
                                            wheelPushData,
                                            curBatchSize,
                                            context.GetGlobalDescriptorSet(),
                                            m_SSBODescriptorSet);
        vkCmdEndRenderPass(cmdPrecise);

        vkCmdPipelineBarrier(cmdPrecise,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &colorBarrier);

        // 清空结果缓冲
        vkCmdFillBuffer(cmdPrecise,
                        context.GetCounterBuffer()->GetBuffer(),
                        0,
                        VK_WHOLE_SIZE,
                        0);

        ResultData initResult{};
        initResult.coreRadiusSqBits = 0xFFFFFFFF;
        std::vector<ResultData> initResults(BATCH_LAYER_COUNT, initResult);
        vkCmdUpdateBuffer(cmdPrecise,
                          context.GetResultBuffer()->GetBuffer(),
                          0,
                          sizeof(ResultData) * BATCH_LAYER_COUNT,
                          initResults.data());

        VkBufferMemoryBarrier resClearBarrier{};
        resClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        resClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resClearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        resClearBarrier.buffer = context.GetResultBuffer()->GetBuffer();
        resClearBarrier.size = VK_WHOLE_SIZE;

        VkBufferMemoryBarrier counterClearBarrier{};
        counterClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        counterClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterClearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterClearBarrier.buffer = context.GetCounterBuffer()->GetBuffer();
        counterClearBarrier.size = VK_WHOLE_SIZE;

        std::array<VkBufferMemoryBarrier, 2> barriers = {resClearBarrier,
                                                         counterClearBarrier};

        vkCmdPipelineBarrier(cmdPrecise,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             static_cast<uint32_t>(barriers.size()),
                             barriers.data(),
                             0,
                             nullptr);

        // 派发ComputeFlute提取特征
        SliceComputeInfo computeInfo{context.GetContourDescriptorSet(),
                                     context.GetWidth(),
                                     context.GetHeight(),
                                     MAX_POINTS,
                                     0,
                                     plane.normal,
                                     plane.point,
                                     glm::vec4(0.f)};
        maskRenderSystem.ComputeFlute(cmdPrecise,
                                      computeInfo,
                                      context.GetGlobalDescriptorSet(),
                                      context.GetTipInfoBuffer());

        RENDERDOC_START;

        // 阻塞等待计算完毕
        m_lveDevice.endSingleTimeCommands(cmdPrecise);

        RENDERDOC_END;
#else
        // 将BATCH_LAYER_COUNT数量的位姿矩阵填进SSBO
        memcpy(m_poseSSBOBuffer->GetMappedMemory(),
               m_transformMatrixes.data() + i,
               curBatchSize * sizeof(glm::mat4));

        // ==========================================================
        // 1. 计算宏观相机并更新SSBO (完全无视 BBox)
        // ==========================================================
        CameraData macroCamData = CalculateMicroCamera({0xFFFFFFFF, 0, 0, 0},
                                                       viewConfig,
                                                       context.GetWidth(),
                                                       context.GetHeight(),
                                                       plane);
        std::vector<CameraData> macroCameras(BATCH_LAYER_COUNT, macroCamData);

        // 单向轻量级写入，不产生任何阻塞
        context.UpdateGlobalSSBO(macroCameras.data(),
                                 sizeof(CameraData) * BATCH_LAYER_COUNT);

        // ==========================================================
        // 2. 准备精确渲染阶段 (Precise Pass) 所需的全部变量
        // ==========================================================
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = context.GetMaskRenderPass();
        renderPassInfo.framebuffer = context.GetBlankMaskFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {context.GetWidth(), context.GetHeight()};
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color.uint32[0] = 0u;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        VkViewport viewport{0.0f,
                            0.0f,
                            (float)context.GetWidth(),
                            (float)context.GetHeight(),
                            0.f,
                            1.f};
        VkRect2D scissor{{0, 0}, {context.GetWidth(), context.GetHeight()}};

        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.image = context.GetBlankMaskImage();
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,
                                         0,
                                         1,
                                         0,
                                         BATCH_LAYER_COUNT};
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // ==========================================================
        // 3. 开启 CommandBuffer 并记录精确渲染指令
        // ==========================================================
        VkCommandBuffer cmdPrecise = m_lveDevice.beginSingleTimeCommands();

        vkCmdBeginRenderPass(cmdPrecise, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmdPrecise, 0, 1, &viewport);
        vkCmdSetScissor(cmdPrecise, 0, 1, &scissor);

        OptimizePlaneInfo planeInfo{cmdPrecise,
                                    context.GetGlobalDescriptorSet(),
                                    plane.normal,
                                    plane.point};
        OptimizeDrawInfo blankInfo{cmdPrecise,
                                   m_blank,
                                   glm::mat4(1.0f),
                                   context.GetGlobalDescriptorSet(),
                                   plane.normal,
                                   plane.point};

        maskRenderSystem.BindPlaneInjectionPipeline(cmdPrecise);
        maskRenderSystem.RenderPlaneInjection(planeInfo);

        maskRenderSystem.BindBlankStencilPipeline(cmdPrecise);
        maskRenderSystem.RenderBlank(blankInfo);
        maskRenderSystem.BindBlankColorPipeline(cmdPrecise);
        maskRenderSystem.RenderBlank(blankInfo);

        wheelRenderSystem.RenderBatchWheels(cmdPrecise,
                                            wheelPushData,
                                            curBatchSize,
                                            context.GetGlobalDescriptorSet(),
                                            m_SSBODescriptorSet);
        vkCmdEndRenderPass(cmdPrecise);

        // 渲染完毕后，转换图像布局供 Compute Shader 读取
        vkCmdPipelineBarrier(cmdPrecise,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &colorBarrier);

        // ==========================================================
        // 4. 清空结果缓冲，派发 Compute Shader (特征提取)
        // ==========================================================
        vkCmdFillBuffer(cmdPrecise,
                        context.GetCounterBuffer()->GetBuffer(),
                        0,
                        VK_WHOLE_SIZE,
                        0);

        ResultData initResult{};
        initResult.coreRadiusSqBits = 0xFFFFFFFF;
        std::vector<ResultData> initResults(BATCH_LAYER_COUNT, initResult);
        vkCmdUpdateBuffer(cmdPrecise,
                          context.GetResultBuffer()->GetBuffer(),
                          0,
                          sizeof(ResultData) * BATCH_LAYER_COUNT,
                          initResults.data());

        VkBufferMemoryBarrier resClearBarrier{};
        resClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        resClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resClearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        resClearBarrier.buffer = context.GetResultBuffer()->GetBuffer();
        resClearBarrier.size = VK_WHOLE_SIZE;

        VkBufferMemoryBarrier counterClearBarrier{};
        counterClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        counterClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterClearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterClearBarrier.buffer = context.GetCounterBuffer()->GetBuffer();
        counterClearBarrier.size = VK_WHOLE_SIZE;

        std::array<VkBufferMemoryBarrier, 2> barriers = {resClearBarrier,
                                                         counterClearBarrier};

        vkCmdPipelineBarrier(cmdPrecise,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             static_cast<uint32_t>(barriers.size()),
                             barriers.data(),
                             0,
                             nullptr);

        // 派发ComputeFlute提取特征
        SliceComputeInfo computeInfo{context.GetContourDescriptorSet(),
                                     context.GetWidth(),
                                     context.GetHeight(),
                                     MAX_POINTS,
                                     0,
                                     plane.normal,
                                     plane.point,
                                     glm::vec4(0.f)};
        maskRenderSystem.ComputeFlute(cmdPrecise,
                                      computeInfo,
                                      context.GetGlobalDescriptorSet());
                                      //context.GetTipInfoBuffer());

        RENDERDOC_START;

        // 唯一的一次 CPU 阻塞等待：等待最终特征提取计算完毕
        m_lveDevice.endSingleTimeCommands(cmdPrecise);

        RENDERDOC_END;

        // --- 获取最优解 ---

#endif
        // --- 获取最优解 ---
        for (uint32_t j = 0; j < curBatchSize; ++j) {
            float score = EvaluateFitness(mappedResults[j]);
            if (score > m_bestScore) {
                m_bestScore = score;
                m_bestResult = mappedResults[j];
                m_bestPose = m_transformMatrixes[i + j];
            }
        }
    }

    std::cout << "\n[Optimizer] All batches finished successfully!"
              << "\n";

    std::cout << "\n========== [ Result ] Best Result ==========\n";
    float sq = std::bit_cast<float>(m_bestResult.coreRadiusSqBits);
    std::cout << "core radius: " << std::sqrt(sq) << "\n";
    std::cout << "slot angle: " << m_bestResult.slotAngle << "\n";
    //std::cout << "rake angle: " << m_bestResult.rakeAngle << "\n";

    std::cout << "\n========== [ Result ] Best Pose Matrix ==========\n";
    for (int row = 0; row < 4; ++row) {
        std::cout << "[ ";
        for (int col = 0; col < 4; ++col) {
            // 注意：glm::mat4 的索引是 mat[col][row]
            std::cout << m_bestPose[col][row] << "\t";
        }
        std::cout << "]\n";
    }
    std::cout << "Best Score: " << m_bestScore << "\n";
    std::cout << "=================================================\n\n";

    context.GetBBoxReadbackBuffer()->Unmap();
    context.GetResultBuffer()->Unmap();

    WriteToolPath();

    std::cout << "BATCH_LAYER_COUNT : " << BATCH_LAYER_COUNT << "\n";
}

float GrindingWheelPoseOptimizer::EvaluateFitness(const ResultData& result)
{
    if (result.coreRadiusSqBits == 0xFFFFFFFF) {
        return -999999.0f;
    }

    float coreRadiusSq = std::bit_cast<float>(result.coreRadiusSqBits);
    float coreRadius = std::sqrt(coreRadiusSq);

    if (coreRadius > 5.f) {
        return -999999.0f;
    }

    float targetSlotAngle = 65.0f;
    float targetArcLength = 5.f * glm::radians(targetSlotAngle);
    float targetCoreRadius = 3.0f;
    // float targetRakeAngle = 10.0f;

    float resultArcLength = glm::radians(result.slotAngle) * 5.f;

    float slotAngleError = std::abs(resultArcLength - targetArcLength);
    float coreRadiusError = std::abs(coreRadius - targetCoreRadius);
    // ArcProjection已经的计算已经保证了前角和螺旋角
    // float rakeAngleError = std::abs(result.rakeAngle - targetRakeAngle);

    float weightSlot = 1.f;
    float weightCore = 1.f;

    float score = -(slotAngleError * weightSlot + coreRadiusError * weightCore);

    return score;
}

void GrindingWheelPoseOptimizer::WriteToolPath()
{
    std::string filename =
        "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\optimize_toolpath.txt";
    
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing!\n";
        return;
    }

    outFile << "new seg\n";

    outFile << std::fixed << std::setprecision(6);

    int num = 20;
    float stepX = 0.2f;
    for (int stepIndex = 0; stepIndex < num; ++stepIndex) {
        // --- 1. 计算位移和旋转角 (与 Vertex Shader 保持绝对一致) ---
        float stepDist = static_cast<float>(stepIndex) * stepX;
        float theta = (stepDist * tan(glm::radians(30.f))) / 5.f;

        // --- 2. 构建平移矩阵 ---
        glm::mat4 transX(1.0f);
        transX[3][0] = stepDist;

        // --- 3. 构建绕X轴旋转矩阵 ---
        // 注意 GLM 默认是列主序 [col][row]
        glm::mat4 rotX(1.0f);
        rotX[1][1] = std::cos(theta);
        rotX[1][2] = std::sin(theta);
        rotX[2][1] = -std::sin(theta);
        rotX[2][2] = std::cos(theta);

        // --- 4. 计算当前步的最终位姿 ---
        glm::mat4 finalPose = (transX * rotX) * m_bestPose;

        // --- 5. 提取坐标 (位置) 和 法向量 (X轴) ---
        glm::vec3 p = glm::vec3(finalPose[3]);  // 提取位移向量
        glm::vec3 n = glm::vec3(finalPose[0]);  // 提取局部坐标系的X轴作为法向

        // --- 6. 写入文件 ---
        outFile << p.x << "," << p.y << "," << p.z << "," << n.x << "," << n.y << ","
                << n.z << "\n";
    }

    outFile.close();

    std::cout << "[System] Successfully exported tool path to: " << filename << "\n";
}

GrindingWheelPoseOptimizer::~GrindingWheelPoseOptimizer()
{
}

}  // namespace optimize