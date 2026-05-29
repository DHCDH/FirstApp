#include "SliceProcessor.h"

#include <fstream>

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

    // --- 只有需要显示的截面需要更新相机UBO，用于显示渲染结果 ---
    // 只有在非反演模式下，才允许更新相机UBO用于显示渲染结果
    if (isParametricWheelRequested == false) {
        UpdateCameraUbo(frameData.displayPlane.normal,
                        frameData.displayPlane.point,
                        viewConfig);
    }

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

    INFO("camera info: position(%f, %f, %f), target(%f, %f, %f), up(%f, %f, %f)",
         cameraPos.x,
         cameraPos.y,
         cameraPos.z,
         point.x,
         point.y,
         point.z,
         v.x,
         v.y,
         v.z);

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
    return {};
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

    RENDERDOC_START;

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
    m_rasterizer->GetRenderSystem().ComputeSdfGenerate(
        cmd,
        m_context->GetSdfGenerateDescriptorSet(),
        push,
        m_context->GetWidth(),
        m_context->GetHeight());

    // --- 出图：将SDF数据回读到CPU并保存 ---
    // 1. 插入图像内存屏障：等待 Compute Shader 写入完成
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

    // 2. 创建一个临时的 Host-Visible Buffer 用于接收图像数据
    uint32_t width = m_context->GetWidth();
    uint32_t height = m_context->GetHeight();
    VkDeviceSize imageSize = width * height * sizeof(float);  // UDF 是 R32F 格式
    lve::LveBuffer readbackBuffer(
        m_lveDevice,
        sizeof(float),
        width * height,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 3. 录制拷贝命令：Image -> Buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // 0 表示紧凑排列
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

    // --- 回读保存结束 ---

    m_rasterizer->TransitionImageLayout(cmd,
                                        m_context->GetSdfImage(),
                                        VK_IMAGE_LAYOUT_GENERAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_lveDevice.endSingleTimeCommands(cmd);

    // --- 保存UDF数据 ---
    readbackBuffer.Map();
    float* mappedData = static_cast<float*>(readbackBuffer.GetMappedMemory());

    // 强烈推荐保存为 .bin 二进制文件。
    // 原因：R32F 包含几百万个浮点数，转成 .csv
    // 文本不仅体积暴增(几十MB)，还容易丢失浮点精度。
    std::string outPath =
        "D:/Data/Study/vulkan/FirstApp/output_stuff/wear/udf_output.bin";
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
    // --- 保存UDF完成 ---

    RENDERDOC_END;

    SliceWearFitter::FitConfig config{};
    config.grMin = 0.01f;
    config.grMax = 1.5f;
    config.tolerance = 0.0001f;
    config.maxIterations = 25;

    m_parametricInstancedData = BuildFreshParametricInstancedData(true);

    VkDescriptorSet globalDescriptorSet = m_context->GetGlobalDescriptorSet();

    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances};

    float bestCornerRadius =
        m_wearFitter->FitGrindingWheelCornerRadius(nullptr,
                                                   config,
                                                   m_parametricInstancedData,
                                                   globalDescriptorSet,
                                                   rasterizerData,
                                                   frameData,
                                                   fitViewConfig);

    INFO("Best corner radius: %f", bestCornerRadius);

    // 暂时忽略Analyzer的工作

    return;
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