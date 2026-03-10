#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "LveBuffer.h"
#include "LveDescriptors.h"
#include "LveDevice.h"
#include "LveModel.h"

class SliceRasterizer;
class SliceAnalyzer;

struct ResultData {
    glm::vec2 coreRadiusPoint{0.f};
    glm::vec2 tipPoint;  // 刀尖点
    glm::vec2 tangent;   // 容屑槽刀尖点处的切向量

    glm::vec2 normal;
    glm::vec2 point;

    uint32_t coreRadiusSqBits{std::numeric_limits<
        uint32_t>::max()};  // 芯厚半径平方的位数据，设为float的最大值位表示
    float rakeAngle;                // 前角
    float slotAngle;                // 槽宽角

    float _pad;
};

constexpr uint32_t MAX_PLANES = 300;
constexpr uint32_t MAX_POINTS = 50000;

// --- 管理离屏渲染资源、计算资源、分辨率变更和资源生命周期
class SliceResourceContext
{
    friend class SliceRasterizer;
    friend class SliceAnalyzer;

public:
    SliceResourceContext(lve::LveDevice& lveDevice, uint32_t width, uint32_t height);
    ~SliceResourceContext();

    SliceResourceContext(const SliceResourceContext&) = delete;
    SliceResourceContext& operator=(const SliceResourceContext&) = delete;

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
};