#include "SliceProcessor.h"

#include <fstream>

#include "../performance/PerformanceCollector.h"
#include "Logger.h"
#include "RenderDocHelper.h"

using namespace lve;

static const std::string ORIGINAL_FLUTE_FILE_PATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/wear/flute_1V1_r0.3.txt";

namespace slice
{
SliceProcessor::SliceProcessor(LveDevice& lveDevice, uint32_t width, uint32_t height)
    : m_lveDevice{lveDevice}
{
    m_context = std::make_unique<SliceResourceContext>(lveDevice, width, height);
    m_rasterizer = std::make_unique<SliceRasterizer>(lveDevice, *m_context);
    m_analyzer = std::make_unique<SliceAnalyzer>(lveDevice, *m_context);
    m_camera = std::make_unique<LveCamera>();
    m_wearCalculator =
        std::make_unique<wear::GrindingWheelWearCalculatorNew>(ORIGINAL_FLUTE_FILE_PATH);
    m_wearFitter = std::make_unique<SliceWearFitter>(lveDevice,
                                                     m_rasterizer->GetRenderSystem(),
                                                     *m_context,
                                                     *m_rasterizer);
}

void SliceProcessor::ProcessFrame(VkCommandBuffer commandBuffer,
                                  const SliceFrameData& frameData,
                                  const SliceViewConfig& viewConfig,
                                  bool isAnalysisRequested,
                                  bool isParametricWheelRequested)
{
    // 检查计算是否已读回完毕
    if (!m_analyzer->IsReadyForNewTask()) {
        return;
    }

    if (frameData.planes.empty()) {
        throw std::runtime_error("No planes to process!");
    }

    m_planes = frameData.planes;

    // 重置Fence锁
    m_analyzer->ResetFence();

    // --- 更新显示相机（每帧都更新；反演流程会在其后覆盖为自己的拟合相机）---
    UpdateCameraUbo(frameData.displayPlane.normal,
                    frameData.displayPlane.point,
                    viewConfig);

    // 组装Draw Mask数据
    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances};

    m_parametricInstancedData =
        BuildFreshParametricInstancedData(isParametricWheelRequested);

    m_rasterizer->ProcessAllPlanes(commandBuffer,
                                   *m_context,
                                   rasterizerData,
                                   frameData,
                                   viewConfig,
                                   isAnalysisRequested,
                                   m_parametricInstancedData);

    return;
}

void SliceProcessor::UpdateCameraUbo(const glm::vec3& normal, const glm::vec3& point,
                                     const SliceViewConfig& viewConfig, bool invertUp)
{
    glm::vec3 w = glm::normalize(normal);
    glm::vec3 cameraPos = point + w * 2000.f;
    glm::vec3 hintUp = (std::abs(glm::dot(w, glm::vec3{0.f, 1.f, 0.f})) > 0.99f)
                           ? glm::vec3(0.f, 0.f, 1.f)
                           : glm::vec3{0.f, 1.f, 0.f};
    glm::vec3 u = glm::normalize(glm::cross(hintUp, w));
    glm::vec3 v = glm::cross(w, u);

    if (invertUp) {
        // 砂轮反演需要将up取反才能得到正确的位置，原因未知
        v = -v;
    }

    m_camera->SetViewTarget(cameraPos, point, v);

    m_camera->SetOrthographicProjection(viewConfig.xMin,
                                        viewConfig.xMax,
                                        viewConfig.zMax,
                                        viewConfig.zMin,
                                        -4000.f,
                                        4000.f);

    GlobalUbo ubo{};
    ubo.projection = m_camera->GetProjection();
    ubo.view = m_camera->GetView();
    ubo.inverseView = m_camera->GetInverseView();

    // 调用我们在第1步加的 Context 更新接口
    m_context->UpdateGlobalUbo(&ubo, sizeof(GlobalUbo));
}

VkDescriptorImageInfo SliceProcessor::GetOutputImageInfo()
{
    VkDescriptorImageInfo imageInfo{};
    if (m_context) {
        imageInfo.sampler = m_context->GetSampler();
        imageInfo.imageView = m_context->GetBlankMaskView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    return imageInfo;
}

bool SliceProcessor::GetAnalysisResult(uint32_t numPlanes,
                                       std::vector<ResultData>& results)
{
    if (!m_analyzer || !m_context) {
        std::cerr << "Get analysis result failed!"
                  << "\n";
        return false;
    }

    return m_analyzer->DownloadGPUCalculateResult(*m_context,
                                                  numPlanes,
                                                  m_planes,
                                                  results);
}

const std::vector<glm::vec2>& SliceProcessor::GetContourPoints() const
{
    return m_analyzer->GetContourPoints();
}

const std::vector<std::vector<glm::vec2>>& SliceProcessor::GetAllContourPoints() const
{
    return m_analyzer->GetAllContourPoints();
}

VkFence SliceProcessor::GetComputeFence() const
{
    return m_analyzer->GetFence();
}

void SliceProcessor::Resize(uint32_t width, uint32_t height)
{
    if (m_context) {
        m_context->Resize(width, height);
    }
}

void SliceProcessor::ExecuteWearAnalysis(const std::string& targetFluteFilePath,
                                         const SliceFrameData& frameData)
{
    PROFILE_SCOPE("Wear analysis cost");

    m_wearCalculator->ParseFlutePointSet(targetFluteFilePath);
    glm::vec4 mapInfo = m_wearCalculator->CalculateSdfMapInfo(m_context->GetWidth(),
                                                              m_context->GetHeight());

    float physSize = m_context->GetWidth() * mapInfo.z;

    float centerHorizontalZ = mapInfo.x + physSize * 0.5f;
    float centerVerticalY = mapInfo.y + physSize * 0.5f;
    glm::vec3 targetPoint = glm::vec3(0.0f, centerVerticalY, centerHorizontalZ);

    SliceViewConfig fitViewConfig{};
    fitViewConfig.xMin = -physSize * 0.5f;
    fitViewConfig.xMax = physSize * 0.5f;
    fitViewConfig.zMin = -physSize * 0.5f;  // 对应物理底部
    fitViewConfig.zMax = physSize * 0.5f;   // 对应物理顶部
    fitViewConfig.nX = m_context->GetWidth();
    fitViewConfig.nZ = m_context->GetHeight();

    DEBUG("Set camera info: target(%f, %f, %f)",
          targetPoint.x,
          targetPoint.y,
          targetPoint.z);

    UpdateCameraUbo(glm::vec3(1, 0, 0), targetPoint, fitViewConfig, true);

    auto points = m_wearCalculator->GetFlutePointSet();
    m_context->SetSdfBuffer(points);

    // RENDERDOC_START;

    GpuTimestampPool sdfTimer;
    sdfTimer.Initialize(m_lveDevice.device(), 2);
    sdfTimer.SetTimestampPeriod(m_lveDevice.properties.limits.timestampPeriod);

    VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();

    m_rasterizer->TransitionImageLayout(cmd,
                                        m_context->GetSdfImage(),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_IMAGE_LAYOUT_GENERAL);
    SdfGeneratePushConstants push{targetPoint,
                                  fitViewConfig.xMin,
                                  fitViewConfig.zMax,
                                  (fitViewConfig.xMax - fitViewConfig.xMin) /
                                      static_cast<float>(m_context->GetWidth()),
                                  (fitViewConfig.zMax - fitViewConfig.zMin) /
                                      static_cast<float>(m_context->GetHeight()),
                                  static_cast<uint32_t>(points.size())};

    sdfTimer.Reset(cmd);
    sdfTimer.WriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

    m_rasterizer->GetRenderSystem().ComputeSdfGenerate(
        cmd,
        m_context->GetSdfGenerateDescriptorSet(),
        push,
        m_context->GetWidth(),
        m_context->GetHeight());

    sdfTimer.WriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);

#if 0
    // --- 出图：将SDF数据回读到CPU并保存 ---
    VkImageMemoryBarrier computeToTransferBarrier{};
    computeToTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    computeToTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    computeToTransferBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    computeToTransferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeToTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    computeToTransferBarrier.image = m_context->GetSdfImage();
    computeToTransferBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &computeToTransferBarrier);

    uint32_t width = m_context->GetWidth();
    uint32_t height = m_context->GetHeight();
    VkDeviceSize imageSize = width * height * sizeof(float);
    lve::LveBuffer readbackBuffer(
        m_lveDevice,
        sizeof(float),
        width * height,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmd,
                           m_context->GetSdfImage(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           readbackBuffer.GetBuffer(),
                           1,
                           &region);

    m_rasterizer->TransitionImageLayout(cmd,
                                        m_context->GetSdfImage(),
                                        VK_IMAGE_LAYOUT_GENERAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_lveDevice.endSingleTimeCommands(cmd);

    readbackBuffer.Map();
    float* mappedData = static_cast<float*>(readbackBuffer.GetMappedMemory());

    std::string outPath =
        "D:/Data/Study/vulkan/articles/text/images/chapter5/udf/udf_output.bin";
    std::ofstream outFile(outPath, std::ios::out | std::ios::binary);
    if (outFile.is_open()) {
        outFile.write(reinterpret_cast<const char*>(mappedData), imageSize);
        outFile.close();
        INFO("Successfully exported UDF data to %s (Size: %u bytes)",
             outPath.c_str(),
             imageSize);
    } else {
        ERROR("Failed to open file for UDF export!");
    }
    readbackBuffer.Unmap();
#endif
    // --- 保存UDF完成 ---

    m_rasterizer->TransitionImageLayout(cmd,
                                        m_context->GetSdfImage(),
                                        VK_IMAGE_LAYOUT_GENERAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_lveDevice.endSingleTimeCommands(cmd);

    sdfTimer.ReadResults(0, 2);
    double sdfGenerateGpuUs = sdfTimer.TimestampDeltaUs(0, 1);
    INFO("GPU SDF generate time: %.3f us", sdfGenerateGpuUs);

    // RENDERDOC_END;

    SliceWearFitter::FitConfig config{};
    config.grMin = 0.5f;
    config.grMax = 3.f;
    config.tolerance = 0.001f;
    config.maxIterations = 50;

    m_parametricInstancedData = BuildFreshParametricInstancedData(true);

    VkDescriptorSet globalDescriptorSet = m_context->GetGlobalDescriptorSet();

    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances};

    double inversionGpuUs = 0.0;
    float bestCornerRadius =
        m_wearFitter->FitGrindingWheelCornerRadius(nullptr,
                                                   config,
                                                   m_parametricInstancedData,
                                                   globalDescriptorSet,
                                                   rasterizerData,
                                                   frameData,
                                                   fitViewConfig,
                                                   &inversionGpuUs);

    INFO("Best corner radius: %f", bestCornerRadius);
    INFO("GPU wear inversion loop time: %.3f us", inversionGpuUs);
    INFO("GPU wear inversion total (SDF generate + loop): %.3f us",
         sdfGenerateGpuUs + inversionGpuUs);

#if 1
    // --- GPU轮廓提取：用最优圆角半径渲染并提取容屑槽点集 ---
    {
        auto& searchBuf = m_context->GetSearchStateBuffer();
        if (!searchBuf.GetMappedMemory()) {
            searchBuf.Map();
        }
        GpuSearchState* searchState =
            reinterpret_cast<GpuSearchState*>(searchBuf.GetMappedMemory());
        uint32_t savedFlags = searchState->flags;
        searchState->flags = 0;

        m_parametricInstancedData.candidatesRadius = bestCornerRadius;
        m_parametricInstancedData.extractContour = true;

        VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();
        m_rasterizer->ProcessAllPlanes(cmd,
                                       *m_context,
                                       rasterizerData,
                                       frameData,
                                       fitViewConfig,
                                       true,
                                       m_parametricInstancedData);
        m_lveDevice.endSingleTimeCommands(cmd);

        searchState->flags = savedFlags;
        m_parametricInstancedData.extractContour = false;

        lve::LveBuffer* readbackBuf = m_context->GetReadbackBuffer();
        readbackBuf->Map();
        auto* dataPtr = static_cast<char*>(readbackBuf->GetMappedMemory());

        size_t countOffset = sizeof(ResultData) * MAX_PLANES;
        uint32_t pointCount = *reinterpret_cast<uint32_t*>(dataPtr + countOffset);

        size_t pointsOffset =
            sizeof(ResultData) * MAX_PLANES + sizeof(uint32_t) * MAX_PLANES;
        auto* points = reinterpret_cast<glm::vec2*>(dataPtr + pointsOffset);

        if (pointCount > 0 && pointCount <= MAX_POINTS) {
            std::string outPath =
                "D:/Data/Study/vulkan/articles/text/measurement data/matlab "
                "code/manipulate data/try iteration/"
                "flute_contour_bestR_" +
                std::to_string(bestCornerRadius) + ".txt";
            std::ofstream outFile(outPath);
            if (outFile.is_open()) {
                for (uint32_t i = 0; i < pointCount; i++) {
                    // 反演点位于工件 ZOY(即 Y-Z) 截面。
                    // 依据 shader_sdf_generate.comp 的映射：
                    //   viewX(水平, points[i].x) -> 物理 Z；viewY(垂直, points[i].y) ->
                    //   物理 Y。
                    const float physY = centerVerticalY + points[i].y;  // 工件 Y（切向）
                    const float physZ =
                        centerHorizontalZ + points[i].x;  // 工件 Z（径向）
                    // 输出改为论文坐标系（刀具轴 = Z）的 XOY 平面：
                    //   X = 原 Z（径向），Y = 原 Y（切向），与 2D 仿真输出的 "x, y"
                    //   两列格式一致。
                    outFile << physZ << ", " << physY << "\n";
                }
                outFile.close();
                INFO("Exported %u GPU contour points (bestR=%.4f) to %s",
                     pointCount,
                     bestCornerRadius,
                     outPath.c_str());

                std::string metaPath =
                    "D:/Data/Study/vulkan/articles/text/measurement data/matlab "
                    "code/manipulate data/try iteration/"
                    "flute_contour_meta.txt";
                std::ofstream metaFile(metaPath);
                if (metaFile.is_open()) {
                    metaFile << "phys_size_mm=" << physSize << "\n";
                    metaFile << "center_y=" << centerVerticalY << "\n";
                    metaFile << "center_z=" << centerHorizontalZ << "\n";
                    metaFile << "plane_x=" << targetPoint.x << "\n";
                    metaFile << "best_r=" << bestCornerRadius << "\n";
                    metaFile << "width=" << m_context->GetWidth() << "\n";
                    metaFile << "height=" << m_context->GetHeight() << "\n";
                    metaFile.close();
                    INFO("Exported metadata (physSize=%.4f) to %s",
                         physSize,
                         metaPath.c_str());
                }
            } else {
                ERROR("Failed to open contour export: %s", outPath.c_str());
            }
        } else {
            ERROR("Invalid GPU contour point count: %u", pointCount);
        }

        readbackBuf->Unmap();
    }
#endif

    // 暂时忽略Analyzer的工作

    return;
}

void SliceProcessor::RestoreDisplay(const SliceFrameData& frameData,
                                    const SliceViewConfig& viewConfig)
{
    // 恢复显示相机（反演会把它改成微小的拟合相机且不还原）
    UpdateCameraUbo(frameData.displayPlane.normal,
                    frameData.displayPlane.point,
                    viewConfig);

    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances};

    m_parametricInstancedData = BuildFreshParametricInstancedData(false);

    VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();
    m_rasterizer->ProcessAllPlanes(cmd,
                                   *m_context,
                                   rasterizerData,
                                   frameData,
                                   viewConfig,
                                   false,
                                   m_parametricInstancedData);
    m_lveDevice.endSingleTimeCommands(cmd);
}

ParametricInstancedData SliceProcessor::BuildFreshParametricInstancedData(
    bool isParametricWheelRequested)
{
    ParametricInstancedData pData{};

    pData.useParametricWheel = isParametricWheelRequested;
    pData.unitGridBuffer = m_context->GetUnitGridBuffer()->GetBuffer();
    pData.unitGridIndexBuffer = m_context->GetUnitGridIndexBuffer()->GetBuffer();
    pData.indexCount = m_context->GetUnitGridVertexCount();

    if (m_rasterizer->GetGrndWheelInstancesBuffer() != VK_NULL_HANDLE) {
        pData.instanceBuffer = m_rasterizer->GetGrndWheelInstancesBuffer();
        // 假设你在 Processor 中维护了实例数量，或者从 Buffer 直接获取
        pData.instanceCount = m_rasterizer->GetGrndWheelInstancesCount();
    } else {
        pData.instanceBuffer = VK_NULL_HANDLE;
        pData.instanceCount = 0;
    }

    return pData;
}

SliceProcessor::~SliceProcessor() = default;

}  // namespace slice
