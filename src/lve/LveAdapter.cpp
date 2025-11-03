#include "LveAdapter.h"

#include <qnativeinterface.h>

#include <stdexcept>
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan_win32.h>


LveAdapter::LveAdapter(QWindow* parent) : QWindow(parent) {
    setSurfaceType(QSurface::VulkanSurface);
    resize(1280, 800);
}

void LveAdapter::resizeEvent(QResizeEvent*) {
    m_isResized = true;
}

VkExtent2D LveAdapter::FrameBufferExtent() const {
    const qreal dpr = devicePixelRatio();
    return { uint32_t(width() * dpr), uint32_t(height() * dpr) };
}

void LveAdapter::CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface)
{

    HWND hwnd = reinterpret_cast<HWND>(winId());
    HINSTANCE hinst = GetModuleHandle(nullptr);

    VkWin32SurfaceCreateInfoKHR ci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    ci.hinstance = hinst;
    ci.hwnd = hwnd;

    if (vkCreateWin32SurfaceKHR(instance, &ci, nullptr, surface) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateWin32SurfaceKHR failed");
    }
}
