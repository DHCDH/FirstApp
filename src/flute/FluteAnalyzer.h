#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm.hpp>

#include "LveBuffer.h"
#include "LveDescriptors.h"
#include "LveDevice.h"
#include "LvePipeline.h"

namespace flute
{

constexpr uint32_t MAX_FLUTE_POINTS = 50000;

// 与 shader 中 ResultData 字节级一致的读回结构
struct FluteGeometryResult {
    glm::vec2 coreRadiusPoint{0.f};
    glm::vec2 tipPoint{};
    glm::vec2 tangent{};
    glm::vec2 normal{};
    glm::vec2 point{};
    uint32_t coreRadiusSqBits = 0xFFFFFFFF;  // 芯厚半径平方的位表示
    float rakeAngle = 0.f;                    // 前角 (deg)
    float slotAngle = 0.f;                    // 槽宽角 (deg)
    float _pad = 0.f;
};

// 与 shader 中 Push 块字节级一致
struct FluteComputePushConstants {
    uint32_t maxPoints = MAX_FLUTE_POINTS;
    uint32_t planeIndex = 0;
    alignas(16) glm::vec3 normal{0.f};
    alignas(16) glm::vec3 point{0.f};
    alignas(16) glm::vec4 mapInfo{0.f};
    int isRightCut{1};
};

class FluteAnalyzer
{
public:
    explicit FluteAnalyzer(lve::LveDevice& device);
    ~FluteAnalyzer();

    FluteAnalyzer(const FluteAnalyzer&) = delete;
    FluteAnalyzer& operator=(const FluteAnalyzer&) = delete;

    // 解析容屑槽点集文件（与 wear 的 ParseFlutePointSet 逻辑完全一致）
    bool ParseFlutePointSet(const std::string& filePath);

    // 上传点集并执行 sort -> trace -> align -> rake，读回结果
    bool Analyze(FluteGeometryResult& outResult);

    const std::vector<glm::vec2>& GetPoints() const
    {
        return m_points;
    }

private:
    void CreateComputeResources();
    void CreateComputePipelineLayout();
    void CreateComputePipelines();

    lve::LveDevice& m_lveDevice;

    std::vector<glm::vec2> m_points;

    // 描述符资源
    std::unique_ptr<lve::LveDescriptorSetLayout> m_computeSetLayout = nullptr;
    std::unique_ptr<lve::LveDescriptorPool> m_computePool = nullptr;
    VkDescriptorSet m_computeSet = VK_NULL_HANDLE;

    // 上传 staging buffer
    std::unique_ptr<lve::LveBuffer> m_stagingBuffer = nullptr;

    // 计算缓冲（binding 编号与 shader 一一对应）
    std::unique_ptr<lve::LveBuffer> m_pointsBuffer = nullptr;      // binding 1
    std::unique_ptr<lve::LveBuffer> m_counterBuffer = nullptr;     // binding 2
    std::unique_ptr<lve::LveBuffer> m_resultBuffer = nullptr;      // binding 3 (host visible)
    std::unique_ptr<lve::LveBuffer> m_knnBuffer = nullptr;         // binding 4
    std::unique_ptr<lve::LveBuffer> m_tipInfoBuffer = nullptr;     // binding 5
    std::unique_ptr<lve::LveBuffer> m_tempSortedBuffer = nullptr;  // binding 6
    std::unique_ptr<lve::LveBuffer> m_finalPointsBuffer = nullptr; // binding 7

    VkPipelineLayout m_computePipelineLayout = VK_NULL_HANDLE;
    std::unique_ptr<lve::LvePipeline> m_sortPipeline = nullptr;
    std::unique_ptr<lve::LvePipeline> m_tracePipeline = nullptr;
    std::unique_ptr<lve::LvePipeline> m_alignPipeline = nullptr;
    std::unique_ptr<lve::LvePipeline> m_rakePipeline = nullptr;
};

}  // namespace flute
