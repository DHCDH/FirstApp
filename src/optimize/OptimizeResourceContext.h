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
    void UpdateGlobalSSBO(void* SSBOData, size_t size);

public:
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
    lve::LveBuffer* GetResultBuffer() const
    {
        return m_resultBuffer.get();
    }
    lve::LveBuffer* GetBBoxBuffer() const
    {
        return m_bboxBuffer.get();
    }
    VkRenderPass GetMaskRenderPass() const
    {
        return m_maskRenderPass;
    }
    VkFramebuffer GetBlankMaskFramebuffer() const
    {
        return m_blankMaskFramebuffer;
    }
    VkImage GetBlankMaskImage() const
    {
        return m_blankMaskImage;
    }
    lve::LveBuffer* GetBBoxReadbackBuffer() const
    {
        return m_bboxReadbackBuffer.get();
    }
    lve::LveBuffer* GetCounterBuffer() const
    {
        return m_counterBuffer.get();
    }
    VkDescriptorSet GetContourDescriptorSet() const
    {
        return m_contourDescriptorSet;
    }
    lve::LveBuffer* GetTipInfoBuffer() const
    {
        return m_tipInfoBuffer.get();
    }
    VkDescriptorSetLayout GetContourComputeSetLayout() const
    {
        return m_contourComputeSetLayout->GetDescriptorSetLayout();
    }
    VkDescriptorSetLayout GetBBoxComputeSetLayout() const
    {
        return m_bboxComputeSetLayout->GetDescriptorSetLayout();
    }

private:
    void CreateSampler();          // 创建采样器
    void CreateGlobalResources();  // UBO
    void CreateRenderPass();
    void CreateOffscreenImage();
    void CreateImageResources(VkImage& image, VkDeviceMemory& memory, VkImageView& view,
                              VkFramebuffer& framebuffer);
    void CreateFramebuffers();
    void CreateFramebuffers(VkImageView colorView, VkFramebuffer& framebuffer);
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

    // --- SSBO ---
    std::unique_ptr<lve::LveBuffer> m_cameraSSBOBuffer = nullptr;
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

    std::unique_ptr<lve::LveBuffer> m_bboxBuffer;
    std::unique_ptr<lve::LveBuffer> m_bboxReadbackBuffer;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_bboxComputeSetLayout;
    VkDescriptorSet m_bboxDescriptorSet;
};

}