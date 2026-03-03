#include "SliceAnalyzer.h"

#include <stdexcept>
#include <iostream>
#include <fstream>

using namespace lve;

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
                                               ResultData& outResult)
{
    if (vkGetFenceStatus(m_lveDevice.device(), m_fence) == VK_NOT_READY) {
        std::cout << "fence not ready" << std::endl;
        return false;
    }

    context.m_readbackBuffer->Map();
    auto* dataPtr = static_cast<char*>(context.m_readbackBuffer->GetMappedMemory());

    size_t resultOffset = 0;
    outResult = *reinterpret_cast<ResultData*>(dataPtr);

    size_t countOffset = sizeof(ResultData);
    uint32_t pointCount = *reinterpret_cast<uint32_t*>(dataPtr + countOffset);

    const uint32_t MAX_POINTS = 50000;
    if (pointCount > MAX_POINTS) {
        pointCount = MAX_POINTS;
    }

    if (pointCount > 0) {
        size_t pointsOffset = sizeof(ResultData) + sizeof(uint32_t);
        auto* srcBegin = reinterpret_cast<glm::vec2*>(dataPtr + pointsOffset);

        m_contourPoints.resize(pointCount);

        std::memcpy(m_contourPoints.data(), srcBegin, pointCount * sizeof(glm::vec2));
    } else {
        std::cout << "no points" << std::endl;
        m_contourPoints.clear();
    }

    // 解除映射
    context.m_readbackBuffer->Unmap();

    #if 1
    {
        // 将点外轮廓点写入文件
        std::cout << "Contour points size: " << m_contourPoints.size() << std::endl;
        std::string filepath =
            "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\points.txt";
        std::ofstream outFile(filepath);
        if (!outFile.is_open()) {
            std::cerr << "[ERROR] Failed to open file：" << filepath << std::endl;
            return true;
        }
        for (const auto& pos : m_contourPoints) {
            outFile << "(" << pos.x << ", " << pos.y << ")\n";
        }
        outFile.close();
        if (outFile.fail()) {
            std::cerr << "[ERROR] Write file: " << filepath << " failed" << std::endl;
        } else {
            std::cout << "[SUCCESS] Write coordinates into file：" << filepath << std::endl;
        }
    }
    #endif

    return true;
}

SliceAnalyzer::~SliceAnalyzer()
{
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_lveDevice.device(), m_fence, nullptr);
    }
}