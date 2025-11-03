#pragma once

#include <QWindow>

#include <vulkan/vulkan.h>

class LveAdapter : public QWindow
{
    Q_OBJECT
public:
    explicit LveAdapter(QWindow* parent = nullptr);

    void CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface);

    VkExtent2D FrameBufferExtent() const;
    bool WasWindowResized() const { return m_isResized; }

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    bool m_isResized{ false };
};