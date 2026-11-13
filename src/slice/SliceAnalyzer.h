#pragma once

#include <vector>

#include "../Global.h"
#include "SliceResourceContext.h"
#include "SliceWearFitter.h"

namespace slice
{

class SliceAnalyzer
{
public:
    SliceAnalyzer(lve::LveDevice& device, SliceResourceContext& context);
    ~SliceAnalyzer();

    SliceAnalyzer(lve::LveDevice& device, const SliceResourceContext&) = delete;
    SliceAnalyzer& operator=(const SliceAnalyzer&) = delete;

public:
    // 上一次回读的细分耗时，供 SliceView 统一汇总打印（避免日志顺序倒置）
    struct ReadbackTiming {
        double mapCopyMs = 0.0;   // 映射 staging + 拷贝 ResultData
        double tmpFileMs = 0.0;   // 逐面逐点写 TMP.txt（调试用）
        double collectMs = 0.0;   // 按面切分有序轮廓点
        double unmapMs = 0.0;     // 解除映射
        uint32_t points = 0;      // 本次回读的有效点总数
        bool valid = false;       // 是否已产生过数据
    };

    bool DownloadGPUCalculateResult(SliceResourceContext& context, uint32_t numPlanes,
                                    const std::vector<Plane>& planes,
                                    std::vector<ResultData>& outResult);

    const ReadbackTiming& GetLastReadbackTiming() const
    {
        return m_lastReadbackTiming;
    }

    VkFence GetFence() const
    {
        return m_fence;
    }

    // 检查Fence是否被信号量标记
    bool IsReadyForNewTask() const;

    void ResetFence();

    const std::vector<glm::vec2>& GetContourPoints() const
    {
        return m_contourPoints;
    }

    // 每一面的有序轮廓点（平面局部 2D 坐标），下标与平面索引一一对应
    const std::vector<std::vector<glm::vec2>>& GetAllContourPoints() const
    {
        return m_allContourPoints;
    }

private:
    lve::LveDevice& m_lveDevice;

    // 任务同步锁
    VkFence m_fence = VK_NULL_HANDLE;

    std::unique_ptr<lve::LveBuffer> m_stagingBuffer = nullptr;

    std::vector<glm::vec2> m_contourPoints;
    std::vector<std::vector<glm::vec2>> m_allContourPoints;
    ReadbackTiming m_lastReadbackTiming;
};

}  // namespace slice