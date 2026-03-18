#include "SliceView.h"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <QMetaObject>
#include <QCoreApplication>

using namespace lve;

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

void SliceView::BuildContactMask(const SliceFrameData& frameData)
{
    if (!m_window) return;

    PollAnalysis();

    if (m_isWaitingForAnalysis) {
        // ProcessAnalysisResult 内部会调用 GetAnalysisResult。
        // 它发现 fence 没亮会立刻返回 false，绝不卡顿！如果亮了，就解析数据。
        if (ProcessAnalysisResult(m_analysisPlaneCount)) {
            m_isWaitingForAnalysis = false;  // 拿到数据了，解除等待状态
        }
    }

    // --- 处理窗口大小变化 ---
    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
        m_renderer->RecreateSwapChain();
    }

    // --- 检查当前帧是否触发了Analysis请求 ---
    bool requestThisFrame = false;
    if (m_runningMode == RunningMode::DISPLAY_AND_ANALYSIS && !m_isWaitingForAnalysis) {
        requestThisFrame = true;
        m_isWaitingForAnalysis = true;
        m_analysisPlaneCount = static_cast<uint32_t>(frameData.planes.size());

        m_runningMode = RunningMode::DISPLAY_ONLY;
    }

    // --- 更新Processor状态 ---
    m_processor->SetModels(m_blankModel, m_grndWheelModel);
    m_processor->SetBlankMatrix(frameData.blankMatrix);
    m_processor->SetGrindingWheelInstances(frameData.wheelMatrixes);

    // --- 开启渲染命令录制 ---
    VkCommandBuffer commandBuffer = m_renderer->BeginFrame();
    if (commandBuffer == nullptr) {
        std::cerr << "failed to get command buffer!" << std::endl;
        return;
    }

    // 将requestThisFrame信号传递给Processor
    m_processor->ProcessFrame(commandBuffer, frameData, m_viewConfig, requestThisFrame);

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
                                frameData.displayPlane.point};
        m_overlayRenderSystem->RenderSliceContour(info);
    }

    m_renderer->EndSwapChainRenderPass(commandBuffer);

    // 获取当前提交给GPU的fence
    VkFence computeFence = m_processor->GetComputeFence();

    m_renderer->EndFrame(m_processor->GetComputeFence());
}

bool SliceView::ProcessAnalysisResult(uint32_t numPlanes)
{
    if (numPlanes == 0) {
        return false;
    }

    std::vector<ResultData> results;

    if (m_processor->GetAnalysisResult(numPlanes, results)) {
        // --- 打印统计数据 ---
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "========= GPU Geometry Analysis (" << numPlanes
                  << " Planes) =========\n";

        for (uint32_t i = 0; i < numPlanes; i++) {
            auto resultData = results[i];
            uint32_t distBits = resultData.coreRadiusSqBits;

            if (distBits != 0xFFFFFFFF) {
                float worldDistSq = std::bit_cast<float>(distBits);
                float radius = std::sqrt(worldDistSq);

                std::cout << "Plane[" << i << "]"
                          << "\n";
                std::cout << "Core Radius: " << radius << " mm\n"
                          << "Core Radius Point : (" << resultData.coreRadiusPoint.x
                          << ", " << resultData.coreRadiusPoint.y << ")\n";
                std::cout << "Rake Angle : " << resultData.rakeAngle << " deg\n";
                std::cout << "Tangent : (" << resultData.tangent.x << ", "
                          << resultData.tangent.y << ")\n";
                std::cout << "Slot Angle : " << resultData.slotAngle << " deg\n";
                std::cout << "-----------------------------------------\n";
            } else {
                std::cout << "Plane[" << i << "] No valid intersection.\n";
                std::cout << "-----------------------------------------\n";
            }
        }

        // 成功解析完毕
        return true;
    }
    // fence未就绪，继续等待
    return false;
}

void SliceView::PollAnalysis()
{
    if (m_isWaitingForAnalysis) {
        // 去探查底层 Fence
        if (ProcessAnalysisResult(m_analysisPlaneCount)) {
            m_isWaitingForAnalysis = false;  // 拿到数据，解除等待
        }
    }
}

void SliceView::RunFrame()
{
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