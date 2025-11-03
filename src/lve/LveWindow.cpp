#include "LveWindow.h"

#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <assert.h>

namespace lve {

    LveWindow::LveWindow(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name)
        : m_hwnd{ nativeWindowHandle }, m_hinstance{ nativeInstanceHandle }, m_width{ w }, m_height{ h }, m_windowName{ name }
    {
    }

    LveWindow::~LveWindow()
    {

    }

    void LveWindow::CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface)
    {
        assert(surface && "surface out param must not be null");
        assert(m_hwnd != nullptr && "HWND is null. Call winId() after the widget is shown.");
        assert(m_hinstance != nullptr && "HINSTANCE is null.");

        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.hinstance = (HINSTANCE)m_hinstance;
        createInfo.hwnd = (HWND)m_hwnd;

        if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Win32 surface!");
        }
    }

    //void LveWindow::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    //{
    //    auto lveWindow = reinterpret_cast<LveWindow*>(glfwGetWindowUserPointer(window));
    //    lveWindow->framebufferResized = true;
    //    lveWindow->m_width = m_width;
    //    lveWindow->m_height = m_height;
    //}

}