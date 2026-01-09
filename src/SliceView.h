#pragma once

#include <chrono>
#include <memory>
#include <vulkan/vulkan.h>

#include "Global.h"
#include "lve/LveRenderer.h"
#include "lve/systems/SliceDisplaySystem.h"
#include "lve/systems/SliceMaskRenderSystem.h"

class SliceView
{
public:
    SliceView(lve::LveDevice& device, const SliceViewConfig& config,
              void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h,
              std::string name);
    ~SliceView();

    SliceView(const SliceView&) = delete;
    SliceView& operator=(const SliceView&) = delete;

    void RunFrame();
    void WaitIdle();
    void BuildContactMask(const SliceFrameData& frameData);

    void UpdateSliceViewConfig(const SliceViewConfig& config);
    void SetBlankModel(lve::LveModel* model)
    {
        m_blankModel = model;
    }
    void SetGrindingWheelModel(lve::LveModel* model)
    {
        m_grndWheelModel = model;
    }

    lve::LveWindow* GetWindow() const
    {
        return m_window.get();
    }

private:
    void InitOffscreenResources();
    void InitDisplayResources();

    void CreateSingleMaskResource(VkImage& image, VkDeviceMemory& memory,
                                  VkImageView& view, VkFramebuffer& framebuffer);
    void CreateContactMaskResources();
    void UpdateSliceCamera(const float& sliceHeight);
    void CleanupMaskResource(VkImage&, VkImageView&, VkDeviceMemory&, VkFramebuffer&);
    void UpdateGrindingWheelInstanceBuffer(const std::vector<glm::mat4>& instances);
    void RecreateDisplayDescriptorSet();

    VkFormat FindDepthStencilFormat();

private:
    lve::LveDevice& m_device;
    std::unique_ptr<lve::LveWindow> m_window;
    std::unique_ptr<lve::LveCamera> m_camera;
    std::unique_ptr<lve::LveBuffer> m_cameraUboBuffer;
    std::unique_ptr<lve::SliceMaskRenderSystem> m_sliceMaskRenderSystem;

    std::unique_ptr<lve::LveDescriptorSetLayout> m_setLayout;
    std::unique_ptr<lve::LveDescriptorPool> m_descriptorPool;
    VkDescriptorSet m_descriptorSet;

    std::unique_ptr<lve::LveRenderer> m_renderer;
    std::unique_ptr<lve::LvePipeline> m_displayPipeline;
    std::unique_ptr<lve::SliceDisplaySystem> m_displaySystem;
    std::unique_ptr<lve::LveDescriptorPool> m_displayPool;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_displaySetLayout;
    VkDescriptorSet m_displayDescriptorSet = VK_NULL_HANDLE;
    VkSampler m_displaySampler;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    SliceViewConfig m_viewConfig{};

    float m_frameTimeSec = 0.f;
    std::chrono::high_resolution_clock::time_point m_lastTick{};

    /*深度/模板缓冲资源*/
    VkImage m_depthStencilImage = VK_NULL_HANDLE;
    VkImageView m_depthStencilView = VK_NULL_HANDLE;
    VkDeviceMemory m_depthStencilMemory = VK_NULL_HANDLE;

private:
    lve::LveModel* m_blankModel = nullptr;
    lve::LveModel* m_grndWheelModel = nullptr;
    std::unique_ptr<lve::LveBuffer> m_grndWheelInstanceBuffer;
    uint32_t m_grndWheelInstanceCount{0};

    /*棒料mask*/
    VkImage m_blankMaskImage = VK_NULL_HANDLE;
    VkImageView m_blankMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_blankMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_blankMaskFramebuffer = VK_NULL_HANDLE;

    /*砂轮mask*/
    VkImage m_grndWheelMaskImage = VK_NULL_HANDLE;
    VkImageView m_grndWheelMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_grndWheelMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_grndWheelMaskFramebuffer = VK_NULL_HANDLE;

    /*相交部分mask*/
    VkImage m_contactMaskImage = VK_NULL_HANDLE;
    VkImageView m_contactMaskView = VK_NULL_HANDLE;
    VkDeviceMemory m_contactMaskMemory = VK_NULL_HANDLE;
    VkFramebuffer m_contactMaskFramebuffer = VK_NULL_HANDLE;

    /*共享通用render pass，只有一个color attachment*/
    VkRenderPass m_maskRenderPass = VK_NULL_HANDLE;
};