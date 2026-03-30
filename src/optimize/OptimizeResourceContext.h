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

struct CameraData {
    glm::mat4 projView;
    glm::vec4 mapInfo;
};

struct ResultData {
    glm::vec2 coreRadiusPoint{0.f};
    glm::vec2 tipPoint;  // 刀尖点
    glm::vec2 tangent;   // 容屑槽刀尖点处的切向量

    glm::vec2 normal;
    glm::vec2 point;

    uint32_t coreRadiusSqBits{std::numeric_limits<
        uint32_t>::max()};  // 芯厚半径平方的位数据，设为float的最大值位表示
    float rakeAngle;        // 前角
    float slotAngle;        // 槽宽角

    float _pad;
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

private:
    void CreateComputeResources();

    void CleanupMaskResource(VkImage& image, VkImageView& view, VkDeviceMemory& memory,
                             VkFramebuffer& framebuffer);

private:
    lve::LveDevice& m_lveDevice;
    uint32_t m_width;
    uint32_t m_height;

    std::unique_ptr<lve::LveBuffer> m_resultBuffer = nullptr; // mark

    // compute descriptor
    std::unique_ptr<lve::LveDescriptorSetLayout> m_contourComputeSetLayout = nullptr;   // mark
    std::unique_ptr<lve::LveDescriptorPool> m_computeDescriptorPool = nullptr;  // mark
    VkDescriptorSet m_contourDescriptorSet = VK_NULL_HANDLE;    // mark

    // 纯compute shader
    std::unique_ptr<lve::LveBuffer> m_zMapBuffer;
    std::unique_ptr<lve::LveBuffer> m_triangleBuffer;

    std::unique_ptr<lve::LveBuffer> m_poseSSBOBuffer;
};

}