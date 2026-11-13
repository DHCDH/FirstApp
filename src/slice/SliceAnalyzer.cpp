#include "SliceAnalyzer.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "Logger.h"

using namespace lve;

namespace slice
{
SliceAnalyzer::SliceAnalyzer(lve::LveDevice& device, SliceResourceContext& context)
    : m_lveDevice(device)
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(m_lveDevice.device(), &fenceInfo, nullptr, &m_fence) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create fence!");
    }
}

bool SliceAnalyzer::IsReadyForNewTask() const
{
    return vkGetFenceStatus(m_lveDevice.device(), m_fence) == VK_SUCCESS;
}

void SliceAnalyzer::ResetFence()
{
    vkResetFences(m_lveDevice.device(), 1, &m_fence);
}

bool SliceAnalyzer::DownloadGPUCalculateResult(SliceResourceContext& context,
                                               uint32_t numPlanes,
                                               const std::vector<Plane>& planes,
                                               std::vector<ResultData>& outResult)
{
    if (vkGetFenceStatus(m_lveDevice.device(), m_fence) == VK_NOT_READY) {
        std::cout << "fence not ready" << std::endl;
        return false;
    }

    using clock = std::chrono::high_resolution_clock;
    const auto toMs = [](clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };

    // --- 1) Map + ResultData 拷贝 ---
    const auto t0 = clock::now();
    context.m_readbackBuffer->Map();
    auto* dataPtr = static_cast<char*>(context.m_readbackBuffer->GetMappedMemory());

    outResult.resize(numPlanes);
    std::memcpy(outResult.data(), dataPtr, numPlanes * sizeof(ResultData));
    const auto t1 = clock::now();

    // 解除映射
    // context.m_readbackBuffer->Unmap();

    // --- 拿到计数器数组首地址 ---
    size_t countOffset = sizeof(ResultData) * MAX_PLANES;
    uint32_t* pointCounts = reinterpret_cast<uint32_t*>(dataPtr + countOffset);

    // --- 拿到点集大数组首地址 ---
    size_t pointsOffset = sizeof(ResultData) * MAX_PLANES + sizeof(uint32_t) * MAX_PLANES;
    auto* srcBegin = reinterpret_cast<glm::vec2*>(dataPtr + pointsOffset);

    // 有效点数统计（与下面的写文件/收集无关，纯粹给耗时日志做分母）
    uint32_t totalPoints = 0;
    for (uint32_t i = 0; i < numPlanes; i++) {
        uint32_t c = pointCounts[i];
        totalPoints += (c > MAX_POINTS) ? MAX_POINTS : c;
    }

    // --- 2) 调试用：把每面每个点写成文本 ---
    // 关掉它（改成 false）就能单独看「纯内存拷贝」的成本，与写盘解耦。
    static constexpr bool kDumpTmpPoints = true;
    clock::time_point t2 = t1, t3 = t1;
    if constexpr (kDumpTmpPoints) {
        t2 = clock::now();
        // 将点外轮廓点写入文件
        std::cout << "Contour points size: " << m_contourPoints.size() << std::endl;
        std::string filepath =
            "D:/Data/Study/vulkan/articles/text/measurement data/matlab code/manipulate "
            "data/try iteration/TMP.txt";
        std::ofstream outFile(filepath);

        for (uint32_t i = 0; i < numPlanes; i++) {
            uint32_t count = pointCounts[i];
            if (count > MAX_POINTS) count = MAX_POINTS;

            if (count == 0) continue;

            for (uint32_t p = 0; p < count; p++) {
                glm::vec2 pos = srcBegin[i * MAX_POINTS + p];
                outFile << pos.x << ", " << pos.y << "\n";
            }
        }

        outFile.close();
        if (outFile.fail()) {
            ERROR("Write file: %s failed", filepath.c_str());
        } else {
            INFO("Write coordinates into file: %s", filepath.c_str());
        }
        t3 = clock::now();
    }

    // --- 3) 收集每一面的有序轮廓点，供导出/建模使用 ---
    const auto t4 = clock::now();
    m_allContourPoints.clear();
    m_allContourPoints.resize(numPlanes);
    for (uint32_t i = 0; i < numPlanes; i++) {
        uint32_t count = pointCounts[i];
        if (count > MAX_POINTS) count = MAX_POINTS;
        if (count == 0) continue;
        const glm::vec2* begin = srcBegin + static_cast<size_t>(i) * MAX_POINTS;
        m_allContourPoints[i].assign(begin, begin + count);
    }

    // --- 保存「屏上显示的那一面」的有序轮廓点，供屏上叠加显示 ---
    // 显示面约定为 planes 数组的最后一项（见 Simulation2DDialog::BuildContactMask）
    const uint32_t displayIdx = (numPlanes > 0) ? (numPlanes - 1) : 0;
    if (numPlanes > 0 && pointCounts[displayIdx] > 0) {
        uint32_t count = pointCounts[displayIdx];
        if (count > MAX_POINTS) count = MAX_POINTS;
        const glm::vec2* begin =
            srcBegin + static_cast<size_t>(displayIdx) * MAX_POINTS;
        m_contourPoints.assign(begin, begin + count);
    } else {
        m_contourPoints.clear();
    }

    const auto t5 = clock::now();
    context.m_readbackBuffer->Unmap();
    const auto t6 = clock::now();

    // 与 SliceView 的「4) 回读」对应，这里只记录，由 SliceView 统一打印，
    // 避免明细行出现在汇总块之前。
    m_lastReadbackTiming.mapCopyMs = toMs(t1 - t0);
    m_lastReadbackTiming.tmpFileMs = toMs(t3 - t2);
    m_lastReadbackTiming.collectMs = toMs(t5 - t4);
    m_lastReadbackTiming.unmapMs = toMs(t6 - t5);
    m_lastReadbackTiming.points = totalPoints;
    m_lastReadbackTiming.valid = true;

    return true;
}

SliceAnalyzer::~SliceAnalyzer()
{
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_lveDevice.device(), m_fence, nullptr);
    }
}

}  // namespace slice