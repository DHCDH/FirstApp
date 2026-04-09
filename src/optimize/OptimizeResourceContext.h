#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "LveBuffer.h"
#include "LveDescriptors.h"
#include "LveDevice.h"
#include "LveModel.h"
#include "OptimizeGlobalConfig.h"

namespace optimize
{
#if 1
constexpr uint32_t MAX_POINTS = 10000;
constexpr uint32_t BATCH_LAYER_COUNT = 1024; // 每次提交的粒子群数量（目前为一次性提交完成），暂时先于粒子群大小保持一致
constexpr uint32_t ZMAP_RESOLUTION =
    7200;  // 180度分成7200份，每个theta同时存储对应的rMin和rMax
constexpr uint32_t SWARM_SIZE = 1024;
constexpr uint32_t PSO_ITERATION_COUNT = 15;
#else
constexpr uint32_t MAX_POINTS = 10000;
constexpr uint32_t BATCH_LAYER_COUNT = 2048;  // 粒子群大小
constexpr uint32_t ZMAP_RESOLUTION =
    7200;  // 180度分成7200份，每个theta同时存储对应的rMin和rMax
constexpr uint32_t SWARM_SIZE = 2048;
constexpr uint32_t PSO_ITERATION_COUNT = 3;
#define SINGLE_ITERATION
#endif

struct CameraData {
    glm::mat4 projView;
    glm::vec4 mapInfo;
};

struct BestResultData {
    float coreRadius{std::numeric_limits<float>::max()};  // 芯厚半径
    float slotAngle;                                      // 槽宽角
    float score;
    uint32_t bestPoseIdx;
    glm::mat4 bestMatrix;
};

struct BBoxData {
    uint32_t minX;
    uint32_t minY;
    uint32_t maxX;
    uint32_t maxY;
};

class OptimizeResourceContext
{
public:
    OptimizeResourceContext(lve::LveDevice& lveDevice, uint32_t width, uint32_t height);
    ~OptimizeResourceContext();

    OptimizeResourceContext(const OptimizeResourceContext&) = delete;
    OptimizeResourceContext& operator=(const OptimizeResourceContext&) = delete;

    void Resize(uint32_t newWidth, uint32_t newHeight);

public:
    void SetGrindingWheelParameters(GrindingWheelParameters&& parameters)
    {
        m_grndWheelParameters = std::move(parameters);
    }

    void SetCutterParameters(CutterParameters&& parameters)
    {
        m_cutterParameters = std::move(parameters);
    }

    uint32_t GetWidth() const
    {
        return m_width;
    }
    uint32_t GetHeight() const
    {
        return m_height;
    }
    lve::LveBuffer* GetResultBuffer() const
    {
        return m_resultBuffer.get();
    }
    VkDescriptorSet GetContourDescriptorSet() const
    {
        return m_contourDescriptorSet;
    }
    VkDescriptorSetLayout GetContourComputeSetLayout() const
    {
        return m_contourComputeSetLayout->GetDescriptorSetLayout();
    }
    lve::LveBuffer* GetTriangleBuffer() const
    {
        return m_triangleBuffer.get();
    }
    lve::LveBuffer* GetZMapBuffer() const
    {
        return m_zMapBuffer.get();
    }
    lve::LveBuffer* GetPoseSSBOBuffer() const
    {
        return m_poseSSBOBuffer.get();
    }
    lve::LveBuffer* GetBestResultSSBOBuffer() const
    {
        return m_bestResultSSBOBuffer.get();
    }
    lve::LveBuffer* GetParticleBuffer() const
    {
        return m_particleBuffer.get();
    }
    GrindingWheelParameters GetGrindingWheelParameters() const
    {
        return m_grndWheelParameters;
    }
    CutterParameters GetCutterParameters() const
    {
        return m_cutterParameters;
    }

private:
    void CreateComputeResources();

    void CleanupMaskResource(VkImage& image, VkImageView& view, VkDeviceMemory& memory,
                             VkFramebuffer& framebuffer);

private:
    lve::LveDevice& m_lveDevice;
    uint32_t m_width;
    uint32_t m_height;

    GrindingWheelParameters m_grndWheelParameters;
    CutterParameters m_cutterParameters;

    std::unique_ptr<lve::LveBuffer> m_resultBuffer = nullptr;  // mark

    // compute descriptor
    std::unique_ptr<lve::LveDescriptorSetLayout> m_contourComputeSetLayout =
        nullptr;                                                                // mark
    std::unique_ptr<lve::LveDescriptorPool> m_computeDescriptorPool = nullptr;  // mark
    VkDescriptorSet m_contourDescriptorSet = VK_NULL_HANDLE;                    // mark

    // 纯compute shader
    std::unique_ptr<lve::LveBuffer> m_zMapBuffer;
    std::unique_ptr<lve::LveBuffer> m_triangleBuffer;

    std::unique_ptr<lve::LveBuffer> m_poseSSBOBuffer;

    std::unique_ptr<lve::LveBuffer> m_bestResultSSBOBuffer;

    std::unique_ptr<lve::LveBuffer> m_particleBuffer;
};

}  // namespace optimize