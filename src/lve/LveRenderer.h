#pragma once

#include "lveWindow.h"
#include "LveDevice.h"
#include "LveSwapChain.h"

#include <memory>
#include <vector>
#include <cassert>

namespace lve {

	class LveRenderer {
	public:

		LveRenderer(LveWindow& window, LveDevice& device);
		~LveRenderer();

		LveRenderer(const LveRenderer&) = delete;
		LveRenderer& operator=(const LveRenderer&) = delete;

		/*提供给渲染系统的查询*/
		VkRenderPass GetSwapChainRenderPass() const { return m_lveSwapChain->GetRenderPass(); }
		VkExtent2D GetSwapChainExtent() const { return m_lveSwapChain->GetSwapChainExtent(); }
		float GetAspectRatio() const { return m_lveSwapChain->GetExtentAspectRatio(); }
		VkCommandBuffer GetCurrentCommandBuffer() const {
			assert(m_isFrameStarted && "Cannot get command buffer when frame not in progress");
			return m_commandBuffers[m_currentFrameIndex];
		}

		int GetFrameIndex() const {
			assert(m_isFrameStarted && "Cannot get frame index when frame not in progress");
			return m_currentFrameIndex;
		}

		/*帧生命周期*/
		VkCommandBuffer BeginFrame();
		void EndFrame();
		void BeginSwapChainRenderPass(VkCommandBuffer commandBuffer);
		void EndSwapChainRenderPass(VkCommandBuffer commandBuffer);

		/*状态查询*/
		bool IsFrameInProgress() const { return m_isFrameStarted; }

		void RecreateSwapChain();

	private:
		void CreateCommandBuffers();
		void FreeCommandBuffers();

		/*变量需要从上到下按顺序初始化，从下往上销毁*/
		LveWindow& m_lveWindow;
		LveDevice& m_lveDevice;
		std::unique_ptr<LveSwapChain> m_lveSwapChain; // 修改成窗口可调整大小，为什么要改成unique_ptr
		std::vector<VkCommandBuffer> m_commandBuffers;

		uint32_t m_currentImageIndex;	// 跟踪正在进行的当前帧状态
		int m_currentFrameIndex{0};
		bool m_isFrameStarted{false};
	};

}  // namespace lve