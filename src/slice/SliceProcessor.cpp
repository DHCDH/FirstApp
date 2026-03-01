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
                                  const SliceViewConfig& viewConfig) 
{
    // 检查计算是否已读回完毕
    if (!m_analyzer->IsReadyForNewTask()) {
        return;
    }

    // 重置Fence锁
    m_analyzer->ResetFence();

    UpdateCameraUbo(frameData.normal, frameData.point, viewConfig);

    // 组装Draw Mask数据
    RasterizerData rasterizerData{m_blankModel,
                                  m_grndWheelModel,
                                  m_blankMatrix,
                                  m_grndWheelInstances,
                                  frameData.point,
                                  frameData.normal};
    // 执行光栅化
    m_rasterizer->DrawMask(commandBuffer, *m_context, rasterizerData);

    //计算着色器
    m_rasterizer->DispatchCompute(commandBuffer, *m_context, frameData, viewConfig);

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

bool SliceProcessor::GetAnalysisResult(ResultData& result)
{
    if (!m_analyzer || !m_context) {
        std::cerr << "Get analysis result failed!"
                  << "\n";
        return false;
    }

    return m_analyzer->DownloadGPUCalculateResult(*m_context, result);
}

const std::vector<glm::vec2>& SliceProcessor::GetContourPoints() const
{
    return {};
}

VkFence SliceProcessor::GetComputeFence() const
{
    return m_analyzer->GetFence();
}

SliceProcessor::~SliceProcessor() = default;