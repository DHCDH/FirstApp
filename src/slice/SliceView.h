#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <memory>

#include "../Global.h"
#include "LveRenderer.h"
#include "SliceProcessor.h"
#include "SliceDisplaySystem.h"
#include "SliceMaskRenderSystem.h"
#include "SliceOverlayRenderSystem.h"

namespace slice
{

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

    // 主渲染流程
    void BuildContactMask(const SliceFrameData& frameData);

    // 更新视图配置
    void UpdateSliceViewConfig(const SliceViewConfig& config);

    void SetModel(lve::LveModel* blank, lve::LveModel* grndWheel);

public:
    void SetDisplayWireframe(const bool& display)
    {
        m_displayWireframe = display;
    }
    void SetRunningMode(RunningMode mode)
    {
        m_runningMode = mode;
    }
    void SetParametricWheelRequested(bool isParametricWheel)
    {
        m_isParametricWheel = isParametricWheel;
    }

    lve::LveWindow* GetWindow() const
    {
        return m_window.get();
    }
    std::vector<SliceViewConfig> GetLastMicroConfigs() const
    {
        return m_processor->GetLastMicroConfigs();
    }

private:
    void InitDisplayResources();
    void RecreateDisplayDescriptorSet();
    bool ProcessAnalysisResult(uint32_t numPlanes);

private:
    lve::LveModel* m_blankModel = nullptr;
    lve::LveModel* m_grndWheelModel = nullptr;

    lve::LveDevice& m_device;
    SliceViewConfig m_viewConfig{};

    std::unique_ptr<lve::LveWindow> m_window;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::unique_ptr<lve::LveRenderer> m_renderer;

    std::unique_ptr<SliceProcessor> m_processor;

    std::unique_ptr<lve::SliceOverlayRenderSystem> m_overlayRenderSystem;

    // --- 屏显资源 ---
    std::unique_ptr<lve::SliceDisplaySystem> m_displaySystem;
    std::unique_ptr<lve::LveDescriptorPool> m_displayPool;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_displaySetLayout;
    VkDescriptorSet m_displayDescriptorSet = VK_NULL_HANDLE;

    bool m_isWireFrame = false;
    bool m_displayWireframe = true;
    bool m_isParametricWheel = false;
    RunningMode m_runningMode{RunningMode::DISPLAY_ONLY};
    bool m_isWaitingForAnalysis = false;  // 是否挂起等待GPU
    uint32_t m_analysisPlaneCount = 0;    // 记住派发了多少个面

    std::chrono::high_resolution_clock::time_point m_lastTick;
    float m_frameTimeSec = 0.f;
};

}  // namespace slice