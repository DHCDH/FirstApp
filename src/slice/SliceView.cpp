#include "SliceView.h"

#include <windows.h>
// windows.h must be included before vulkan.h
#include <vulkan/vulkan_win32.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <algorithm>
#include <bit>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>

#include "Logger.h"
#include "RenderDocHelper.h"
#include "SectionExporter.h"

using namespace lve;

namespace
{
// 把逐面分析结果写成 CSV（文件名带时间戳），方便直接拿去画曲线/做表
std::string WriteSectionReportCsv(const std::vector<Plane>& planes,
                                  const std::vector<slice::ResultData>& results)
{
    // 与工程里其它输出一致，固定落到 output_stuff 下（不依赖进程工作目录）
    const std::filesystem::path dir =
        "D:/Data/Study/vulkan/FirstApp/output_stuff/section_analysis";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmBuf);

    const std::filesystem::path path =
        dir / ("sections_" + std::string(stamp) + ".csv");

    std::ofstream out(path);
    if (!out.is_open()) {
        ERROR("Failed to open section report: %s", path.string().c_str());
        return {};
    }

    out << "index,point_x,point_y,point_z,normal_x,normal_y,normal_z,"
           "has_intersection,core_radius_mm,core_point_x,core_point_y,"
           "rake_angle_deg,slot_angle_deg\n";
    out << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < results.size(); ++i) {
        const slice::ResultData& r = results[i];
        const Plane plane = (i < planes.size()) ? planes[i] : Plane{};
        const bool valid = (r.coreRadiusSqBits != 0xFFFFFFFFu);

        float radius = 0.f;
        if (valid) radius = std::sqrt(std::bit_cast<float>(r.coreRadiusSqBits));

        out << i << ',' << plane.point.x << ',' << plane.point.y << ',' << plane.point.z
            << ',' << plane.normal.x << ',' << plane.normal.y << ',' << plane.normal.z
            << ',' << (valid ? 1 : 0) << ',' << (valid ? radius : 0.f) << ','
            << (valid ? r.coreRadiusPoint.x : 0.f) << ','
            << (valid ? r.coreRadiusPoint.y : 0.f) << ','
            << (valid ? r.rakeAngle : 0.f) << ',' << (valid ? r.slotAngle : 0.f)
            << '\n';
    }

    out.close();

    // 返回绝对路径，方便在控制台里直接看到文件落在哪
    std::error_code absEc;
    const std::filesystem::path absPath = std::filesystem::absolute(path, absEc);
    return absEc ? path.string() : absPath.string();
}
}  // namespace

namespace slice
{
SliceView::SliceView(lve::LveDevice& device, const SliceViewConfig& config,
                     void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h,
                     std::string name)
    : m_device(device), m_viewConfig(config)
{
    m_window =
        std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);

    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = static_cast<HINSTANCE>(nativeInstanceHandle);
    createInfo.hwnd = static_cast<HWND>(nativeWindowHandle);
    if (vkCreateWin32SurfaceKHR(m_device.getVkInstance(),
                                &createInfo,
                                nullptr,
                                &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create slice view surface!");
    }

    m_renderer = std::make_unique<LveRenderer>(*m_window, m_device, m_surface);
    m_processor =
        std::make_unique<SliceProcessor>(m_device, m_viewConfig.nX, m_viewConfig.nZ);
    m_overlayRenderSystem = std::make_unique<SliceOverlayRenderSystem>(
        m_device,
        m_renderer->GetSwapChainRenderPass(),
        m_processor->GetGlobalDescriptorSetLayout());
    m_contourRenderSystem = std::make_unique<SliceContourRenderSystem>(
        m_device,
        m_renderer->GetSwapChainRenderPass());

    // --- 初始化显示系统 ---
    InitDisplayResources();
    RecreateDisplayDescriptorSet();

    m_lastTick = std::chrono::high_resolution_clock::now();
}

void SliceView::SetModel(lve::LveModel* blank, lve::LveModel* grndWheel)
{
    if (!blank || !grndWheel) {
        std::cerr << "Blank or Grinding Wheel model is null"
                  << "\n";
    }
    m_blankModel = blank;
    m_grndWheelModel = grndWheel;
}

void SliceView::InitDisplayResources()
{
    m_displayPool = LveDescriptorPool::Builder(m_device)
                        .SetMaxSets(10)
                        .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20)
                        .Build();
    m_displaySetLayout = LveDescriptorSetLayout::Builder(m_device)
                             .AddBinding(0,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_SHADER_STAGE_FRAGMENT_BIT)
                             .AddBinding(1,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_SHADER_STAGE_FRAGMENT_BIT)
                             .Build();
    m_displaySystem = std::make_unique<SliceDisplaySystem>(
        m_device,
        m_renderer->GetSwapChainRenderPass(),
        m_displaySetLayout->GetDescriptorSetLayout());
}

void SliceView::RecreateDisplayDescriptorSet()
{
    // --- 向Processor索要渲染好的离屏图像信息
    VkDescriptorImageInfo imageInfo = m_processor->GetOutputImageInfo();
    VkDescriptorImageInfo imageInfoEdge = imageInfo;  // 边缘纹理

    lve::LveDescriptorWriter writer(*m_displaySetLayout, *m_displayPool);
    writer.WriteImage(0, &imageInfo);
    writer.WriteImage(1, &imageInfoEdge);

    /*判断是否存在DescriptorSet*/
    if (m_displayDescriptorSet != VK_NULL_HANDLE) {
        writer.Overwrite(m_displayDescriptorSet);
    } else {
        writer.Build(m_displayDescriptorSet);
    }
}

void SliceView::UpdateSliceViewConfig(const SliceViewConfig& config)
{
    // 如果分辨率没变，只更新范围
    if (config.nX == m_viewConfig.nX && config.nZ == m_viewConfig.nZ) {
        m_viewConfig = config;
        return;
    }

    m_viewConfig = config;
    WaitIdle();

    // 如果物理分辨率变了，你需要通知 Processor 里的 Context Resize
    // 需要你在 Processor 里加一个 Resize 接口透传到 Context
    m_processor->Resize(m_viewConfig.nX, m_viewConfig.nZ);

    RecreateDisplayDescriptorSet();
}

void SliceView::BuildContactMask(const SliceFrameData& frameData,
                                 bool isWearEstimationRequested,
                                 std::string wearEstimationFluteFile)
{
    if (!m_window) return;

    m_lastDisplayPlane = frameData.displayPlane;

    // --- 处理窗口大小变化 ---
    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
        m_renderer->RecreateSwapChain();
    }

    // --- 检查当前帧是否触发了Analysis请求 ---
    bool requestThisFrame = false;
    if (m_runningMode == RunningMode::DISPLAY_AND_ANALYZE && !m_isWaitingForAnalysis) {
        requestThisFrame = true;
        m_isWaitingForAnalysis = true;
        m_analysisPlaneCount = static_cast<uint32_t>(frameData.planes.size());
        m_pendingAnalysisPlanes = frameData.planes;
        m_analysisStartTime = std::chrono::high_resolution_clock::now();
        // 调用方没给「点击时刻」时，退化为从录制开始计
        if (m_analysisRequestTime.time_since_epoch().count() == 0) {
            m_analysisRequestTime = m_analysisStartTime;
        }

        m_runningMode = RunningMode::DISPLAY_ONLY;
    }

    // --- 更新Processor状态 ---
    m_processor->SetModels(m_blankModel, m_grndWheelModel);
    m_processor->SetBlankMatrix(frameData.blankMatrix);
    m_processor->SetGrindingWheelInstances(frameData.wheelMatrixes);
    // 截面类型：容屑槽 / 仅轮廓，决定是否执行特征参数提取
    m_processor->SetFluteFeatureEnabled(m_fluteFeatureEnabled);

    RENDERDOC_START;

    // --- 开启渲染命令录制 ---
    VkCommandBuffer commandBuffer = m_renderer->BeginFrame();
    if (commandBuffer == nullptr) {
        std::cerr << "failed to get command buffer!" << std::endl;
        return;
    }

    // 将requestThisFrame信号传递给Processor
    // 屏上显示永远用真实砂轮；参数化砂轮只在 ExecuteWearAnalysis 反演内部使用
    m_processor->ProcessFrame(commandBuffer,
                              frameData,
                              m_viewConfig,
                              requestThisFrame,
                              false);

    // 屏上显示
    m_renderer->BeginSwapChainRenderPass(commandBuffer);

    m_displaySystem->Render(commandBuffer,
                            m_displayDescriptorSet,
                            m_viewConfig.nX,
                            m_viewConfig.nZ,
                            false);  // 线框显示移交OverlayRenderSystem

    if (m_displayWireframe && m_grndWheelModel) {
        SliceInstancedInfo info{commandBuffer,
                                *m_grndWheelModel,
                                m_processor->GetGrndWheelInstancesBuffer(),
                                m_processor->GetGrndWheelInstancesCount(),
                                m_processor->GetGlobalDescriptorSet(),
                                frameData.displayPlane.normal,
                                frameData.displayPlane.point,
                                1};  // 每隔X个实例显示1个线框
        m_overlayRenderSystem->RenderSliceContour(info);
    }

    if (m_hasContourPoints && m_displayContour) {
        m_contourRenderSystem->Render(
            commandBuffer,
            {m_viewConfig.xMin, m_viewConfig.xMax, m_viewConfig.zMin, m_viewConfig.zMax});
    }

    m_renderer->EndSwapChainRenderPass(commandBuffer);

    // 获取当前提交给GPU的fence
    VkFence computeFence = m_processor->GetComputeFence();

    m_renderer->EndFrame(m_processor->GetComputeFence());
    m_analysisSubmitTime = std::chrono::high_resolution_clock::now();

    RENDERDOC_END;

    // --- 砂轮磨损反演（一次性：只由 Wear Estimation 按钮触发的那一帧执行）---
    if (isWearEstimationRequested && !wearEstimationFluteFile.empty()) {
        WaitIdle();

        m_processor->ExecuteWearAnalysis(wearEstimationFluteFile, frameData);

        // 反演污染了离屏遮罩与共享相机，用显示相机重渲染恢复
        m_processor->RestoreDisplay(frameData, m_viewConfig);
    }

    // --- 后台线程等待fence并触发UI回调 ---
    if (requestThisFrame && computeFence != VK_NULL_HANDLE) {
        VkDevice vkDevice = m_device.device();
        uint32_t planeCount = m_analysisPlaneCount;

        std::thread([this, vkDevice, computeFence, planeCount]() {
            vkWaitForFences(vkDevice, 1, &computeFence, VK_TRUE, UINT64_MAX);
            // GPU 侧全部完成的时刻。用捕获传递，避免跨线程写成员。
            const auto gpuDoneAt = std::chrono::high_resolution_clock::now();
            // --- 通过Qt安全队列把任务扔回主线程 ---
            QMetaObject::invokeMethod(
                qApp,
                [this, planeCount, gpuDoneAt] {
                    this->m_analysisGpuDoneTime = gpuDoneAt;
                    this->ProcessAnalysisResult(planeCount);
                    this->m_isWaitingForAnalysis = false;
                },
                Qt::QueuedConnection);
        }).detach();
    }
}

void SliceView::RePresent()
{
    if (!m_window || !m_renderer || !m_displaySystem) return;

    // --- 处理窗口大小变化 ---
    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
        m_renderer->RecreateSwapChain();
    }

    VkCommandBuffer commandBuffer = m_renderer->BeginFrame();
    if (commandBuffer == nullptr) {
        return;
    }

    // 只重新采样已算好的离屏遮罩，不重跑 ProcessFrame 的实例化求交
    m_renderer->BeginSwapChainRenderPass(commandBuffer);

    m_displaySystem->Render(commandBuffer,
                            m_displayDescriptorSet,
                            m_viewConfig.nX,
                            m_viewConfig.nZ,
                            false);

    if (m_displayWireframe && m_grndWheelModel) {
        SliceInstancedInfo info{commandBuffer,
                                *m_grndWheelModel,
                                m_processor->GetGrndWheelInstancesBuffer(),
                                m_processor->GetGrndWheelInstancesCount(),
                                m_processor->GetGlobalDescriptorSet(),
                                m_lastDisplayPlane.normal,
                                m_lastDisplayPlane.point,
                                1};
        m_overlayRenderSystem->RenderSliceContour(info);
    }

    if (m_hasContourPoints && m_displayContour) {
        m_contourRenderSystem->Render(
            commandBuffer,
            {m_viewConfig.xMin, m_viewConfig.xMax, m_viewConfig.zMin, m_viewConfig.zMax});
    }

    m_renderer->EndSwapChainRenderPass(commandBuffer);
    m_renderer->EndFrame();
}

bool SliceView::ProcessAnalysisResult(uint32_t numPlanes)
{
    if (numPlanes == 0) {
        return false;
    }

    std::vector<ResultData> results;

    if (m_processor->GetAnalysisResult(numPlanes, results)) {
        const std::vector<Plane>& planes = m_pendingAnalysisPlanes;

        INFO("========= Plane Analysis: %u plane(s) [%s] =========",
             static_cast<unsigned>(planes.size()),
             m_fluteFeatureEnabled ? "flute" : "contour only");

        // --- 每个截面打印成一块：几何(点/法向) + 容屑槽特征(如果有) ---
        const size_t rowCount = std::max(planes.size(), results.size());
        for (size_t i = 0; i < rowCount; ++i) {
            const Plane plane = (i < planes.size()) ? planes[i] : Plane{};
            const ResultData r =
                (i < results.size()) ? results[i] : ResultData{};

            INFO("Plane[%u] point=(%.4f, %.4f, %.4f) normal=(%.4f, %.4f, %.4f)",
                 static_cast<unsigned>(i),
                 static_cast<double>(plane.point.x),
                 static_cast<double>(plane.point.y),
                 static_cast<double>(plane.point.z),
                 static_cast<double>(plane.normal.x),
                 static_cast<double>(plane.normal.y),
                 static_cast<double>(plane.normal.z));

            // 仅轮廓模式没有特征提取，几何打完就下一个
            if (!m_fluteFeatureEnabled) {
                continue;
            }

            if (r.coreRadiusSqBits == 0xFFFFFFFFu) {
                INFO("        No valid intersection.");
                continue;
            }

            const float radius = std::sqrt(std::bit_cast<float>(r.coreRadiusSqBits));
            INFO("        Core Radius: %.6f mm  Core Radius Point: (%.6f, %.6f)",
                 static_cast<double>(radius),
                 static_cast<double>(r.coreRadiusPoint.x),
                 static_cast<double>(r.coreRadiusPoint.y));
            INFO("        Rake Angle: %.6f deg  Tangent: (%.6f, %.6f)",
                 static_cast<double>(r.rakeAngle),
                 static_cast<double>(r.tangent.x),
                 static_cast<double>(r.tangent.y));
            INFO("        Slot Angle: %.6f deg", static_cast<double>(r.slotAngle));
        }

        // --- 容屑槽模式才写特征 CSV；仅轮廓模式不产生特征数据 ---
        if (m_fluteFeatureEnabled) {
            const std::string csvPath =
                WriteSectionReportCsv(m_pendingAnalysisPlanes, results);
            if (!csvPath.empty()) {
                INFO("Section report (%u planes) written to %s",
                     numPlanes,
                     csvPath.c_str());
            }
        }

        // 保存读回的有序轮廓点，用于屏上叠加显示
        const auto& contourPoints = m_processor->GetContourPoints();
        if (!contourPoints.empty()) {
            m_contourRenderSystem->UpdatePoints(contourPoints);
            m_hasContourPoints = true;
        }

        // 回读、解析、CSV、屏显轮廓都已就绪 —— 在这里结算端到端耗时。
        // 放在回调之前，避免把导出对话框里用户操作文件的时间也算进来。
        ReportAnalysisTiming(numPlanes);

        // 结果已就绪，通知 UI 层（例如"算完就弹保存框导出"）
        if (m_onAnalysisFinished) {
            m_onAnalysisFinished();
        }

        // 成功解析完毕
        return true;
    }
    // fence未就绪，继续等待
    return false;
}

void SliceView::ReportAnalysisTiming(uint32_t planes)
{
    using clock = std::chrono::high_resolution_clock;
    const auto toMs = [](clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };

    const auto now = clock::now();

    // 四个区间：点击→开始录制（UI 准备）、录制、GPU 等待、回读与解析
    const double prepareMs = toMs(m_analysisStartTime - m_analysisRequestTime);
    const double recordMs = toMs(m_analysisSubmitTime - m_analysisStartTime);
    const double gpuMs = toMs(m_analysisGpuDoneTime - m_analysisSubmitTime);
    const double readbackMs = toMs(now - m_analysisGpuDoneTime);
    const double totalMs = toMs(now - m_analysisRequestTime);
    const double perPlaneMs =
        planes > 0 ? totalMs / static_cast<double>(planes) : 0.0;
    const auto pct = [totalMs](double v) {
        return totalMs > 0.0 ? v * 100.0 / totalMs : 0.0;
    };

    INFO("[Timing] ===== 截面分析：%u 面 =====", static_cast<unsigned>(planes));
    INFO("[Timing]   总耗时 %.1f ms，平均 %.1f ms/面", totalMs, perPlaneMs);
    INFO("[Timing]   1) 准备 %8.1f ms %5.1f%%  点击后到开始录制（界面准备、重建视口）",
         prepareMs,
         pct(prepareMs));
    INFO("[Timing]   2) 录制 %8.1f ms %5.1f%%  把全部绘制/计算命令写进命令缓冲并提交",
         recordMs,
         pct(recordMs));
    INFO("[Timing]   3) GPU  %8.1f ms %5.1f%%  等显卡跑完：找交集框 → 逐面渲染 → 算轮廓与前角",
         gpuMs,
         pct(gpuMs));
    INFO("[Timing]   4) 回读 %8.1f ms %5.1f%%  结果拷回内存 + 写 CSV/日志 + 屏显轮廓更新",
         readbackMs,
         pct(readbackMs));

    // 「4) 回读」的内部构成（由 SliceAnalyzer 记录，这里统一打印）
    const auto& rb = m_processor->GetLastReadbackTiming();
    if (rb.valid) {
        INFO("[Timing]      其中：映射+拷贝 %8.1f | 写 TMP.txt %8.1f | 切分轮廓 %6.1f | 解除映射 %5.1f ms（%u 点）",
             rb.mapCopyMs,
             rb.tmpFileMs,
             rb.collectMs,
             rb.unmapMs,
             rb.points);
    }

    // --- 跨多次分析的累计统计（本次运行内）---
    m_timingRuns++;
    m_timingTotalSumMs += totalMs;
    if (m_timingRuns == 1) {
        m_timingMinMs = totalMs;
        m_timingMaxMs = totalMs;
    } else {
        m_timingMinMs = std::min(m_timingMinMs, totalMs);
        m_timingMaxMs = std::max(m_timingMaxMs, totalMs);
    }
    INFO("[Timing] 累计 %u 次：最近 %.1f / 平均 %.1f / 最小 %.1f / 最大 %.1f ms",
         m_timingRuns,
         totalMs,
         m_timingTotalSumMs / static_cast<double>(m_timingRuns),
         m_timingMinMs,
         m_timingMaxMs);

    // 本次已经结算，清掉起点，避免下一次请求误用旧时刻
    m_analysisRequestTime = clock::time_point{};
}

std::filesystem::path SliceView::ExportSections(const std::filesystem::path& filePath)
{
    if (m_pendingAnalysisPlanes.empty()) {
        ERROR("Export sections failed: no analysis result, run Display&&Calculate first.");
        return {};
    }

    const auto& allPoints = m_processor->GetAllContourPoints();
    if (allPoints.empty()) {
        ERROR("Export sections failed: contour point set is empty.");
        return {};
    }

    const size_t count = std::min(m_pendingAnalysisPlanes.size(), allPoints.size());
    std::vector<SectionExportSection> sections;
    sections.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        SectionExportSection s;
        s.planePoint = m_pendingAnalysisPlanes[i].point;
        s.planeNormal = m_pendingAnalysisPlanes[i].normal;
        s.orderedPoints = allPoints[i];
        sections.push_back(std::move(s));
    }

    if (m_blankRadius <= 0.0) {
        ERROR("Export sections: blank radius is not set, the closing arc in CAD "
              "will be wrong.");
    }

    SectionExportOptions options;
    options.blankRadius = m_blankRadius;
    options.source = "Vulkan 2D slice simulation";

    return SectionExporter::Write(filePath, options, sections);
}

void SliceView::RunFrame(){
    auto now = std::chrono::high_resolution_clock::now();
    m_frameTimeSec =
        std::chrono::duration<float, std::chrono::seconds::period>(now - m_lastTick)
            .count();
    m_lastTick = now;
}

void SliceView::WaitIdle()
{
    vkDeviceWaitIdle(m_device.device());
}

void SliceView::RunWearFittingTask(const std::string& path)
{ 
    WaitIdle();

    //m_processor->ExecuteWearAnalysis(path, frameData);
}

SliceView::~SliceView()
{
    WaitIdle();

    m_displaySystem.reset();
    m_renderer.reset();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_device.getVkInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

}  // namespace slice
