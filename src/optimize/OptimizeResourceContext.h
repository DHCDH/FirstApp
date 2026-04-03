#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "LveBuffer.h"
#include "LveDescriptors.h"
#include "LveDevice.h"
#include "LveModel.h"

namespace optimize
{
constexpr uint32_t MAX_POINTS = 50000;
constexpr uint32_t BATCH_LAYER_COUNT = 1024;  // 粒子群大小
constexpr uint32_t ZMAP_RESOLUTION =
    7200;  // 180度分成7200份，每个theta同时存储对应的rMin和rMax
constexpr uint32_t SWARM_SIZE = 40000;

struct CameraData {
    glm::mat4 projView;
    glm::vec4 mapInfo;
};

struct BestResultData {
    float coreRadius{std::numeric_limits<float>::max()};  // 芯厚半径
    float slotAngle;                                      // 槽宽角
    float score;
    uint32_t bestPoseIdx;
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

private:
    void CreateComputeResources();

    void CleanupMaskResource(VkImage& image, VkImageView& view, VkDeviceMemory& memory,
                             VkFramebuffer& framebuffer);

private:
    lve::LveDevice& m_lveDevice;
    uint32_t m_width;
    uint32_t m_height;

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