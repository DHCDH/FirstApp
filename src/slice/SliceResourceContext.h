#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "../Global.h"
#include "LveBuffer.h"
#include "LveDescriptors.h"
#include "LveDevice.h"
#include "LveModel.h"

namespace slice
{
class SliceRasterizer;
class SliceAnalyzer;
class SliceWearFitter;

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

constexpr uint32_t MAX_PLANES = 300;
constexpr uint32_t MAX_POINTS = 50000;
constexpr uint32_t MAX_CANDIDATES = 128;

// --- 管理离屏渲染资源、计算资源、分辨率变更和资源生命周期
class SliceResourceContext
{
    friend class SliceRasterizer;
    friend class SliceAnalyzer;
    friend class SliceWearFitter;

public:
    SliceResourceContext(lve::LveDevice& lveDevice, uint32_t width, uint32_t height);
    ~SliceResourceContext();

    SliceResourceContext(const SliceResourceContext&) = delete;
    SliceResourceContext& operator=(const SliceResourceContext&) = delete;

    std::unique_ptr<lve::LveBuffer> m_bboxBuffer;
    std::unique_ptr<lve::LveBuffer> m_bboxReadbackBuffer;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_bboxComputeSetLayout;
    VkDescriptorSet m_bboxDescriptorSet;

public:
    // 调整分辨率
    void Resize(uint32_t newWidth, uint32_t newHeight);

    void UpdateGlobalUbo(void* uboData, size_t size);

    uint32_t GetWidth() const
    {
        return m_width;
    }
    uint32_t GetHeight() const
    {
        return m_height;
    }
    VkSampler GetSampler() const
    {
        return m_maskSampler;
    }
    VkImageView GetBlankMaskView() const
    {
        return m_blankMaskView;
    }
    VkDescriptorSet GetGlobalDescriptorSet() const
    {
        return m_globalDescriptorSet;
    }
    VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const
    {
        return m_globalSetLayout->GetDescriptorSetLayout();
    }
    VkDescriptorSet GetBBoxDescriptorSet() const
    {
        return m_bboxDescriptorSet;
    }

    lve::LveBuffer* GetUnitGridBuffer() const
    {
        return m_unitGridBuffer.get();
    }
    lve::LveBuffer* GetUnitGridIndexBuffer() const
    {
        return m_unitGridIndexBuffer.get();
    }
    uint32_t GetUnitGridVertexCount() const
    {
        return m_unitGridVertexCount;
    }

    // --- SDF ---
    VkImage GetSdfImage() const
    {
        return m_sdfImage;
    }
    VkImageView GetSdfImageView() const
    {
        return m_sdfImageView;
    }
    VkSampler GetSdfSampler() const
    {
        return m_sdfSampler;
    }
    VkDescriptorSet GetSdfDescriptorSet() const
    {
        return m_sdfDescriptorSet;
    }
    lve::LveBuffer& GetSdfBuffer() const
    {
        return *m_sdfPointBuffer;
    }
    void SetSdfBuffer(const std::vector<glm::vec4>& points)
    {
        m_sdfPointBuffer->WriteToBuffer(const_cast<glm::vec4*>(points.data()),
                                        points.size() * sizeof(glm::vec4));
    }
    VkDescriptorSet GetSdfGenerateDescriptorSet() const
    {
        return m_sdfGenerateDescriptorSet;
    }

private:
    void CreateSampler();          // 创建采样器
    void CreateGlobalResources();  // UBO
    void CreateRenderPass();
    void CreateOffscreenImage();
    void CreateSingleImageResource(VkImage& image, VkDeviceMemory& memory,
                                   VkImageView& view, VkFramebuffer& framebuffer);
    void CreateFramebuffers();
    void CreateSingleFramebuffer(VkImageView colorView, VkFramebuffer& framebuffer);
    void CreateComputeResources();

    void CleanupMaskResource(VkImage& image, VkImageView& view, VkDeviceMemory& memory,
                             VkFramebuffer& framebuffer);

    void CreateUnitGridBuffer();

    void CreateSdfResources();
    void CleanupSdfResources();

    void CreateSdfGenerateResources();

private:
    lve::LveDevice& m_lveDevice;
    uint32_t m_width;
    uint32_t m_height;

    VkRenderPass m_maskRenderPass = VK_NULL_HANDLE;
    VkFormat m_depthStencilFormat;  // 显卡支持的缓存格式

    // 采样器
    VkSampler m_maskSampler = VK_NULL_HANDLE;

    // --- 图像资源 ---
    // 棒料mask
    VkImage m_blankMaskImage = VK_NULL_HANDLE;
    VkImageView m_blankMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_blankMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_blankMaskFramebuffer = VK_NULL_HANDLE;

    // 砂轮mask
    VkImage m_grndWheelMaskImage = VK_NULL_HANDLE;
    VkImageView m_grndWheelMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_grndWheelMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_grndWheelMaskFramebuffer = VK_NULL_HANDLE;

    // 相交部分mask
    VkImage m_contactMaskImage = VK_NULL_HANDLE;
    VkImageView m_contactMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_contactMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_contactMaskFramebuffer = VK_NULL_HANDLE;

    // 深度/模板缓冲
    VkImage m_depthStencilImage = VK_NULL_HANDLE;
    VkImageView m_depthStencilView = VK_NULL_HANDLE;
    VkDeviceMemory m_depthStencilMemory = VK_NULL_HANDLE;

    // 专用于采样的模板视图
    VkImageView m_stencilSampleView = VK_NULL_HANDLE;

    // --- UBO ---
    std::unique_ptr<lve::LveBuffer> m_cameraUboBuffer = nullptr;
    std::unique_ptr<lve::LveDescriptorPool> m_globalPool = nullptr;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_globalSetLayout = nullptr;
    VkDescriptorSet m_globalDescriptorSet = VK_NULL_HANDLE;

    // --- 计算资源 ---
    // compute buffers
    std::unique_ptr<lve::LveBuffer> m_contourPointsBuffer = nullptr;
    std::unique_ptr<lve::LveBuffer> m_counterBuffer = nullptr;
    std::unique_ptr<lve::LveBuffer> m_resultBuffer = nullptr;
    // 获取前角用到的缓冲指针
    std::unique_ptr<lve::LveBuffer> m_knnBuffer = nullptr;
    std::unique_ptr<lve::LveBuffer> m_tipInfoBuffer = nullptr;
    std::unique_ptr<lve::LveBuffer> m_tempSortedBuffer = nullptr;
    std::unique_ptr<lve::LveBuffer> m_sortedPointsBuffer = nullptr;

    // compute descriptor
    std::unique_ptr<lve::LveDescriptorSetLayout> m_contourComputeSetLayout = nullptr;
    std::unique_ptr<lve::LveDescriptorPool> m_computeDescriptorPool = nullptr;
    VkDescriptorSet m_contourDescriptorSet = VK_NULL_HANDLE;

    // 回读专用staging buffer
    std::unique_ptr<lve::LveBuffer> m_readbackBuffer = nullptr;

    std::unique_ptr<lve::LveBuffer> m_unitGridBuffer;
    std::unique_ptr<lve::LveBuffer> m_unitGridIndexBuffer;
    uint32_t m_unitGridVertexCount{0};

    // --- SDF ---
    VkImage m_sdfImage = VK_NULL_HANDLE;
    VkDeviceMemory m_sdfImageMemory = VK_NULL_HANDLE;
    VkImageView m_sdfImageView = VK_NULL_HANDLE;
    VkSampler m_sdfSampler = VK_NULL_HANDLE;

    std::unique_ptr<lve::LveDescriptorSetLayout> m_sdfSetLayout = nullptr;
    VkDescriptorSet m_sdfDescriptorSet = VK_NULL_HANDLE;

    std::unique_ptr<lve::LveBuffer> m_lossBuffer = nullptr;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_sdfLossSetLayout = nullptr;
    VkDescriptorSet m_sdfLossDescriptorSet = VK_NULL_HANDLE;

    std::unique_ptr<lve::LveBuffer> m_sdfPointBuffer;
    VkDescriptorSet m_sdfGenerateDescriptorSet = VK_NULL_HANDLE;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_sdfGenerateSetLayout;
};

}  // namespace slice