#pragma once

#include <vector>

#include "../Global.h"
#include "SliceResourceContext.h"

class SliceAnalyzer
{
public:
    SliceAnalyzer(lve::LveDevice& device, SliceResourceContext& context);
    ~SliceAnalyzer();

    SliceAnalyzer(lve::LveDevice& device, const SliceResourceContext&) = delete;
    SliceAnalyzer& operator=(const SliceAnalyzer&) = delete;

public:
    bool DownloadGPUCalculateResult(SliceResourceContext& context, uint32_t numPlanes,
                                    const std::vector<Plane>& planes,
                                    std::vector<ResultData>& outResult);

    VkFence GetFence() const
    {
        return m_fence;
    }

    // 检查Fence是否被信号量标记
    bool IsReadyForNewTask() const;

    void ResetFence();

private:
    lve::LveDevice& m_lveDevice;

    // 任务同步锁
    VkFence m_fence = VK_NULL_HANDLE;

    std::unique_ptr<lve::LveBuffer> m_stagingBuffer = nullptr;

    std::vector<glm::vec2> m_contourPoints;
};