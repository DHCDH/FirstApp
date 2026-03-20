#include "SliceAnalyzer.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

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

    context.m_readbackBuffer->Map();
    auto* dataPtr = static_cast<char*>(context.m_readbackBuffer->GetMappedMemory());

    outResult.resize(numPlanes);
    std::memcpy(outResult.data(), dataPtr, numPlanes * sizeof(ResultData));

    // 解除映射
    // context.m_readbackBuffer->Unmap();

    // --- 拿到计数器数组首地址 ---
    size_t countOffset = sizeof(ResultData) * MAX_PLANES;
    uint32_t* pointCounts = reinterpret_cast<uint32_t*>(dataPtr + countOffset);

    // --- 拿到点集大数组首地址 ---
    size_t pointsOffset = sizeof(ResultData) * MAX_PLANES + sizeof(uint32_t) * MAX_PLANES;
    auto* srcBegin = reinterpret_cast<glm::vec2*>(dataPtr + pointsOffset);

#if 1
    {
        // 将点外轮廓点写入文件
        std::cout << "Contour points size: " << m_contourPoints.size() << std::endl;
        std::string filepath =
            "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\points.txt";
        std::ofstream outFile(filepath);

        uint32_t totalPoints = 0;

        for (uint32_t i = 0; i < numPlanes; i++) {
            uint32_t count = pointCounts[i];
            if (count > MAX_POINTS) count = MAX_POINTS;

            if (count == 0) continue;

            totalPoints += count;

            outFile << "plane : p(" << planes[i].point.x << ", " << planes[i].point.y
                    << ", " << planes[i].point.z << "), n(" << planes[i].normal.x << ", "
                    << planes[i].normal.y << ", " << planes[i].normal.z << ")\n";

            for (uint32_t p = 0; p < count; p++) {
                glm::vec2 pos = srcBegin[i * MAX_POINTS + p];
                outFile << "(" << pos.x << ", " << pos.y << ")"
                        << "\n";
            }
        }

        outFile.close();
        if (outFile.fail()) {
            std::cerr << "[ERROR] Write file: " << filepath << " failed" << std::endl;
        } else {
            std::cout << "[SUCCESS] Write coordinates into file：" << filepath
                      << std::endl;
        }
    }
#endif

    context.m_readbackBuffer->Unmap();

    return true;
}

SliceAnalyzer::~SliceAnalyzer()
{
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_lveDevice.device(), m_fence, nullptr);
    }
}

}  // namespace slice