#include "SliceView.h"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include <fstream>
#include <iostream>
#include <limits>

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
    // m_processor->Resize(m_viewConfig.nX, m_viewConfig.nZ);

    RecreateDisplayDescriptorSet();
}

void SliceView::BuildContactMask(const SliceFrameData& frameData)
{
    if (!m_window) return;

    // --- 处理窗口大小变化 ---
    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
        m_renderer->RecreateSwapChain();
    }

    // 异步读回
    ProcessAnalysisResult(frameData);

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

    // Processor执行底层管线
    m_processor->ProcessFrame(commandBuffer, frameData, m_viewConfig);

    // 屏上显示
    m_renderer->BeginSwapChainRenderPass(commandBuffer);

    m_displaySystem->Render(commandBuffer,
                            m_displayDescriptorSet,
                            m_viewConfig.nX,
                            m_viewConfig.nZ,
                            false);  // 线框显示移交OverlayRenderSystem

    if (m_displayWireframe && m_grndWheelModel) {
        uint32_t displayIdx = frameData.displayPlaneIdx;
        if (displayIdx >= frameData.planes.size()) {
            displayIdx = 0;
        }

        SliceInstancedInfo info{commandBuffer,
                                *m_grndWheelModel,
                                m_processor->GetGrndWheelInstancesBuffer(),
                                m_processor->GetGrndWheelInstancesCount(),
                                m_processor->GetGlobalDescriptorSet(),
                                frameData.planes[displayIdx].normal,
                                frameData.planes[displayIdx].point};
        m_overlayRenderSystem->RenderSliceContour(info);
    }

    m_renderer->EndSwapChainRenderPass(commandBuffer);
    m_renderer->EndFrame(m_processor->GetComputeFence());
}

void SliceView::ProcessAnalysisResult(const SliceFrameData& frameData)
{
    if (!m_fetchContour) {
        return;
    }

    if (frameData.planes.empty()) {
        return;
    }

    std::vector<ResultData> results;
    uint32_t numPlanes = static_cast<uint32_t>(frameData.planes.size());

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

                glm::vec3 p = frameData.planes[i].point;
                glm::vec3 n = frameData.planes[i].normal;

                std::cout << "Plane[" << i << "] : p{" << p[0] << ", " << p[1] << ", "
                          << p[2] << "} n{" << n[0] << ", " << n[1] << ", " << n[2] << "}"
                          << "\n";
                std::cout << "Core Radius: " << radius << " mm\n"
                          << "Core Radius Point : (" << resultData.coreRadiusPoint.x
                          << ", " << resultData.coreRadiusPoint.y << ")\n";
                std::cout << "Rake Angle : " << resultData.rakeAngle << " deg\n";
                std::cout << "Tangent : (" << resultData.tangent.x << ", "
                          << resultData.tangent.y << ")\n";
                std::cout << "Slot Angle : " << resultData.slotAngle << " deg\n";
                std::cout << "-----------------------------------------\n";
            }
        }

        m_fetchContour = false;
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