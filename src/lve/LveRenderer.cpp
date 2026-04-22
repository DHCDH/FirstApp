#include "LveRenderer.h"

#include <stdexcept>
#include <array>
#include <iostream>

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

namespace lve {

LveRenderer::LveRenderer(LveWindow& window, LveDevice& device, VkSurfaceKHR surface)
    : m_lveWindow(window), m_lveDevice(device), m_surface(surface)
{
    RecreateSwapChain();
    CreateCommandBuffers(); // 为每个SwapChain图像创建并录制一份命令缓冲
}

LveRenderer::~LveRenderer()
{
    FreeCommandBuffers();
}

void LveRenderer::RecreateSwapChain()
{
    auto extent = m_lveWindow.GetExtent();

    /*等待尺寸有效*/
    while ((extent.width == 0 || extent.height == 0)) {
        /*不进行阻塞等待，直接返回，下一帧再尝试重建*/
        return;
    }

    /*等待GPU空闲，再重建SwapChain*/
    vkDeviceWaitIdle(m_lveDevice.device());

    //lveSwapChain.reset();
    if (m_lveSwapChain == nullptr) {
        m_lveSwapChain = std::make_unique<LveSwapChain>(m_lveDevice, extent, m_surface);
    }
    else {
        std::shared_ptr<LveSwapChain> oldSwapChain = std::move(m_lveSwapChain);
        /*新的SwapChain从oldSwapChain直接继承surface*/
        m_lveSwapChain = std::make_unique<LveSwapChain>(m_lveDevice, extent, oldSwapChain);

        if (!oldSwapChain->CompareSwapFormats(*m_lveSwapChain.get())) {
            // it would probably be better to set up a callback function to notifing the app that a new imcompatible render pass has been created
            throw std::runtime_error("Swap chain image format has changed!");
        }
    }

    
}

/* 为每个SwapChain图像创建并录制一份命令缓冲
    * 申请命令缓冲 → 开始录制 → 开始渲染通道 → 绑定管线 → 发出一次绘制（三角形）→ 结束渲染通道 → 结束录制
    */
void LveRenderer::CreateCommandBuffers()
{
    /*为每个SwapChain图像分配一个主级命令缓冲*/
    m_commandBuffers.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT);   // 按图像数量准备容器，SwapChain里面有imageCout()张图像

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // 主级命令缓冲
    allocInfo.commandPool = m_lveDevice.getCommandPool();
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    /*分配出来的N条CB，依次放进commandBuffers[0, N-1]中*/
    if (vkAllocateCommandBuffers(m_lveDevice.device(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }

}

void LveRenderer::FreeCommandBuffers()
{
    vkFreeCommandBuffers(
        m_lveDevice.device(),
        m_lveDevice.getCommandPool(),
        static_cast<uint32_t>(m_commandBuffers.size()),
        m_commandBuffers.data());
    m_commandBuffers.clear();
}

VkCommandBuffer LveRenderer::BeginFrame()
{
    assert(!m_isFrameStarted && "Can't call beginFrame while already in progress");

    /*向交换链要一张可渲染图像*/
    VkResult result = m_lveSwapChain->AcquireNextImage(&m_currentImageIndex);

    /*窗口大小改变，重建交换链*/
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return nullptr;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    m_isFrameStarted = true;

    auto commandBuffer = GetCurrentCommandBuffer(); // 取出当前帧使用的主级命令缓冲
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    /*开始录制*/
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    return commandBuffer;
}

void LveRenderer::EndFrame(VkFence additionalFence)
{
    assert(m_isFrameStarted && "Can't call endFrame while frame is not in progress");

    auto commandBuffer = GetCurrentCommandBuffer();
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    auto result = m_lveSwapChain->SubmitCommandBuffers(&commandBuffer, &m_currentImageIndex);

    // --- 利用空提交触发附加的Fence ---
    if (additionalFence != VK_NULL_HANDLE) {
        VkSubmitInfo emptySubmit{};
        emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        // 紧跟着向同一个队列提交空任务，当这个队列排空时，就会点亮 additionalFence
        if (vkQueueSubmit(m_lveDevice.graphicsQueue(),
                          1,
                          &emptySubmit,
                          additionalFence) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to submit additional compute fence!");
        }
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_lveWindow.WasWindowResized()) {
        m_lveWindow.ResetWindowResizedFlag();
        RecreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_isFrameStarted = false;
    m_currentFrameIndex = (m_currentFrameIndex + 1) % LveSwapChain::MAX_FRAMES_IN_FLIGHT;
}

void LveRenderer::BeginSwapChainRenderPass(VkCommandBuffer commandBuffer)
{
    assert(m_isFrameStarted && "Can't call beginSwapChainRenderPass if frame is not in progress");
    assert(
        commandBuffer == GetCurrentCommandBuffer() && 
        "Can't begin render pass on command buffer from a different frame");

    /*录制渲染通道*/
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_lveSwapChain->GetRenderPass();
    renderPassInfo.framebuffer = m_lveSwapChain->GetFrameBuffer(m_currentImageIndex);

    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_lveSwapChain->GetSwapChainExtent();

    std::array<VkClearValue, 2> clearValues{};
    // 设置背景颜色
    #if 1
    clearValues[0].color = { 1.f, 1.f, 1.f, 1.0f };
    #else
    clearValues[0].color = { 0.f, 0.f, 0.f, 1.0f };
    #endif
    clearValues[1].depthStencil = { 1.0f, 0 };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_lveSwapChain->GetSwapChainExtent().width);
    viewport.height = static_cast<float>(m_lveSwapChain->GetSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{ {0, 0}, m_lveSwapChain->GetSwapChainExtent() };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void LveRenderer::EndSwapChainRenderPass(VkCommandBuffer commandBuffer)
{
    assert(m_isFrameStarted && "Can't call endSwapChainRenderPass if frame is not in progress");
    assert(
        commandBuffer == GetCurrentCommandBuffer() &&
        "Can't begin render pass on command buffer from a different frame");

    vkCmdEndRenderPass(commandBuffer);
}

VkCommandBuffer LveRenderer::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandPool = m_lveDevice.getCommandPool();
    allocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_lveDevice.device(), &allocateInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void LveRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_lveDevice.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_lveDevice.graphicsQueue());

    vkFreeCommandBuffers(m_lveDevice.device(), m_lveDevice.getCommandPool(), 1, &commandBuffer);

}

}