#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <string>

namespace lve {

	class LveWindow {
	public:
		LveWindow(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
		~LveWindow();

		LveWindow(const LveWindow&) = delete;
		LveWindow& operator=(const LveWindow&) = delete;

		VkExtent2D GetExtent() { return { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) }; }
		bool WasWindowResized() { return m_framebufferResized; }
		void ResetWindowResizedFlag() { m_framebufferResized = false; }

		void CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface);

		void NotifyResized(int w, int h) {m_width = w; m_height = h; m_framebufferResized = true;}

	private:
		// static void framebufferResizeCallback(GLFWwindow* m_window, int width, int height);

		void* m_hwnd;
		void* m_hinstance;
		int m_width;
		int m_height;
		bool m_framebufferResized = false;

		std::string m_windowName;
	};
}