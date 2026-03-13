#include "SliceProcessor.h"

using namespace lve;

SliceProcessor::SliceProcessor(LveDevice& lveDevice, uint32_t width, uint32_t height)
    : m_lveDevice{lveDevice}
{
    m_context = std::make_unique<SliceResourceContext>(lveDevice, width, height);
    m_rasterizer = std::make_unique<SliceRasterizer>(lveDevice, *m_context);
    m_analyzer = std::make_unique<SliceAnalyzer>(lveDevice, *m_context);
    m_camera = std::make_unique<LveCamera>();
}

void SliceProcessor::ProcessFrame(VkCommandBuffer commandBuffer,
                                  const SliceFrameData& frameData,
                                  const SliceViewConfig& viewConfig,
                                  bool isAnalysisRequested)
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

    // --- 只有需要显示的截面需要更新相机UBO，用于显示渲染结果
    UpdateCameraUbo(frameData.displayPlane.normal,
                    frameData.displayPlane.point,
                    viewConfig);

    // 组装Draw Mask数据
    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances};

    m_rasterizer->ProcessAllPlanes(commandBuffer,
                                   *m_context,
                                   rasterizerData,
                                   frameData,
                                   viewConfig,
                                   isAnalysisRequested);

    return;
}

void SliceProcessor::UpdateCameraUbo(const glm::vec3& normal, const glm::vec3& point,
                                     const SliceViewConfig& viewConfig)
{
    glm::vec3 w = glm::normalize(normal);
    glm::vec3 cameraPos = point + w * 2000.f;
    glm::vec3 hintUp = (std::abs(glm::dot(w, glm::vec3{0.f, 1.f, 0.f})) > 0.99f)
                           ? glm::vec3(0.f, 0.f, 1.f)
                           : glm::vec3{0.f, 1.f, 0.f};
    glm::vec3 u = glm::normalize(glm::cross(hintUp, w));
    glm::vec3 v = glm::cross(w, u);

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

    return m_analyzer->DownloadGPUCalculateResult(*m_context, numPlanes, m_planes, results);
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

SliceProcessor::~SliceProcessor() = default;