#pragma once

#include <vulkan/vulkan.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

#include "../Global.h"
#include "LveRenderer.h"
#include "SliceContourRenderSystem.h"
#include "SliceDisplaySystem.h"
#include "SliceMaskRenderSystem.h"
#include "SliceOverlayRenderSystem.h"
#include "SliceProcessor.h"
#include "wear/GrindingWheelWearCalculatorNew.h"

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
    // wearEstimationFluteFile 非空且 isWearEstimationRequested 为 true 时，
    // 会在本帧渲染结束后顺带执行一次 GPU 磨损反演（一次性，不常驻）
    void BuildContactMask(const SliceFrameData& frameData,
                          bool isWearEstimationRequested,
                          std::string wearEstimationFluteFile);

    // 轻量重绘：只重新提交已算好的离屏遮罩与线框，不重跑实例化求交
    void RePresent();

    // 把最近一次分析得到的各截面有序轮廓导出成 OCC 可读文件
    // filePath 由调用方（对话框）决定；返回实际路径，没有数据时返回空 path
    std::filesystem::path ExportSections(const std::filesystem::path& filePath);

    // 棒料半径，导出时写进文件头（OCC 侧闭合圆弧要用）
    void SetBlankRadius(double radius)
    {
        m_blankRadius = radius;
    }

    // 一次分析（含逐面结果回读）完成后回调，UI 层用它接"算完就导出"
    void SetAnalysisFinishedCallback(std::function<void()> callback)
    {
        m_onAnalysisFinished = std::move(callback);
    }

    // 端到端计时起点（用户点击 Display&&Calculate 的那一刻）。
    // 不设置时，SliceView 会在录制开始处自行取时间。
    void SetAnalysisRequestTime(std::chrono::high_resolution_clock::time_point t)
    {
        m_analysisRequestTime = t;
    }

    // 更新视图配置
    void UpdateSliceViewConfig(const SliceViewConfig& config);

    void SetModel(lve::LveModel* blank, lve::LveModel* grndWheel);

    void RunWearFittingTask(const std::string& path);

public:
    void SetDisplayWireframe(const bool& display)
    {
        m_displayWireframe = display;
    }
    void SetDisplayContour(const bool& display)
    {
        m_displayContour = display;
    }
    void SetDisplayRawContour(const bool& display)
    {
        m_displaySystem->SetRawContourEnabled(display);
    }
    void SetDisplayGrid(const bool& display)
    {
        m_displaySystem->SetGridEnabled(display);
    }
    void SetRunningMode(RunningMode mode)
    {
        m_runningMode = mode;
    }

    // 截面类型开关
    // true : 容屑槽 —— 亚像素轮廓 + 芯厚半径 / 槽宽角 / 前角
    // false: 仅轮廓 —— 只做亚像素轮廓提取与工件坐标转换
    void SetFluteFeatureEnabled(bool enabled)
    {
        m_fluteFeatureEnabled = enabled;
    }
    bool IsFluteFeatureEnabled() const
    {
        return m_fluteFeatureEnabled;
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

    // 结算并打印一次端到端分析耗时（分区 + 跨次累计统计）
    void ReportAnalysisTiming(uint32_t planes);

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
    std::unique_ptr<lve::SliceContourRenderSystem> m_contourRenderSystem;
    bool m_hasContourPoints = false;

    // --- 屏显资源 ---
    std::unique_ptr<lve::SliceDisplaySystem> m_displaySystem;
    std::unique_ptr<lve::LveDescriptorPool> m_displayPool;
    std::unique_ptr<lve::LveDescriptorSetLayout> m_displaySetLayout;
    VkDescriptorSet m_displayDescriptorSet = VK_NULL_HANDLE;

    bool m_isWireFrame = false;
    bool m_displayWireframe = true;
    bool m_displayContour = true;
    bool m_fluteFeatureEnabled = true;  // 默认容屑槽仿真
    double m_blankRadius = 0.0;         // 棒料半径，导出截面用
    std::function<void()> m_onAnalysisFinished;  // 分析结果就绪回调（可为空）
    Plane m_lastDisplayPlane{};  // 缓存最近一次的显示截面，用于轻量重绘线框
    RunningMode m_runningMode{RunningMode::DISPLAY_ONLY};
    bool m_isWaitingForAnalysis = false;  // 是否挂起等待GPU
    uint32_t m_analysisPlaneCount = 0;    // 记住派发了多少个面
    // 发起分析请求时快照的截面列表：结果回来时用它，避免期间被新的显示帧覆盖
    std::vector<Plane> m_pendingAnalysisPlanes;

    std::chrono::high_resolution_clock::time_point m_lastTick;
    float m_frameTimeSec = 0.f;

    std::chrono::high_resolution_clock::time_point m_analysisStartTime;
    std::chrono::high_resolution_clock::time_point m_analysisRequestTime{};  // 点击时刻
    std::chrono::high_resolution_clock::time_point m_analysisSubmitTime{};   // 提交完成
    std::chrono::high_resolution_clock::time_point m_analysisGpuDoneTime{};  // fence 通过

    // 跨多次分析的累计统计（本次运行内）
    uint32_t m_timingRuns = 0;
    double m_timingTotalSumMs = 0.0;
    double m_timingMinMs = 0.0;
    double m_timingMaxMs = 0.0;

    std::unique_ptr<wear::GrindingWheelWearCalculatorNew> m_wearCalculator;
};

}  // namespace slice
