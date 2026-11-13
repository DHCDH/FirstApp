#pragma once

#include <memory>

#include "../Global.h"
#include "LveDevice.h"
#include "SliceAnalyzer.h"
#include "SliceRasterizer.h"
#include "SliceResourceContext.h"
#include "SliceWearFitter.h"
#include "wear/GrindingWheelWearCalculatorNew.h"

namespace slice
{
// 资源分配
class SliceProcessor
{
public:
    SliceProcessor(lve::LveDevice& device, uint32_t width, uint32_t height);
    ~SliceProcessor();

    // --- 核心调度流程 ---
    void ProcessFrame(VkCommandBuffer commandBuffer, const SliceFrameData& frameData,
                      const SliceViewConfig& viewConfig, bool isAnalysisRequested,
                      bool isParametricWheelRequested);

    // --- 数据输出 ---
    bool GetAnalysisResult(uint32_t numPlanes, std::vector<ResultData>& results);
    const std::vector<glm::vec2>& GetContourPoints() const;

    // 每一面的有序轮廓点（平面局部 2D），下标与平面索引对应
    const std::vector<std::vector<glm::vec2>>& GetAllContourPoints() const;

    // 最近一次提交的截面列表（与结果数组一一对应）
    const std::vector<Plane>& GetPlanes() const
    {
        return m_planes;
    }

    // --- 获取离屏渲染图象 ---
    VkDescriptorImageInfo GetOutputImageInfo();

    // --- 获取计算完成同步信号 ---
    VkFence GetComputeFence() const;

    void Resize(uint32_t width, uint32_t height);

    void ExecuteWearAnalysis(const std::string& targetFluteFilePath,
                             const SliceFrameData& frameData);

    // 反演会污染离屏遮罩与共享相机，此方法用显示相机 + 真实砂轮实例重渲染恢复
    void RestoreDisplay(const SliceFrameData& frameData,
                        const SliceViewConfig& viewConfig);

public:
    void SetModels(lve::LveModel* blank, lve::LveModel* grndWheel)
    {
        m_blankModel = blank;
        m_grndWheelModel = grndWheel;
    }

    void SetBlankMatrix(const glm::mat4& matrix)
    {
        m_blankMatrix = matrix;
    }

    // 容屑槽特征提取开关：false 时只做亚像素轮廓提取 + 工件坐标转换
    void SetFluteFeatureEnabled(bool enabled)
    {
        m_fluteFeatureEnabled = enabled;
        if (m_rasterizer) {
            m_rasterizer->SetFluteFeatureEnabled(enabled);
        }
    }
    bool IsFluteFeatureEnabled() const
    {
        return m_fluteFeatureEnabled;
    }

    void SetGrindingWheelInstances(const std::vector<glm::mat4>& instances)
    {
        m_grndWheelInstances = instances;

        // 立即上传给光栅化器
        if (m_rasterizer && !m_grndWheelInstances.empty()) {
            m_rasterizer->UpdateInstances(m_grndWheelInstances);
        } else {
            std::cerr << "rasterizer is not ready or instances are empty!"
                      << "\n";
        }
    }

    VkBuffer GetGrndWheelInstancesBuffer() const
    {
        return m_rasterizer->GetGrndWheelInstancesBuffer();
    }
    uint32_t GetGrndWheelInstancesCount() const
    {
        return m_rasterizer->GetGrndWheelInstancesCount();
    }
    VkDescriptorSet GetGlobalDescriptorSet() const
    {
        return m_context->GetGlobalDescriptorSet();
    }
    VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const
    {
        return m_context->GetGlobalDescriptorSetLayout();
    }
    const std::vector<SliceViewConfig>& GetLastMicroConfigs() const
    {
        return m_rasterizer->GetLastMicroConfigs();
    }

private:
    void UpdateCameraUbo(const glm::vec3& normal, const glm::vec3& point,
                         const SliceViewConfig& viewConfig, bool invertUp = false);

    ParametricInstancedData BuildFreshParametricInstancedData(bool isParametricRequested);

    lve::LveDevice& m_lveDevice;

    lve::LveModel* m_blankModel = nullptr;
    lve::LveModel* m_grndWheelModel = nullptr;
    glm::mat4 m_blankMatrix{1.f};
    std::vector<glm::mat4> m_grndWheelInstances;
    std::vector<Plane> m_planes;
    bool m_fluteFeatureEnabled{true};

    std::unique_ptr<SliceResourceContext> m_context = nullptr;
    std::unique_ptr<SliceRasterizer> m_rasterizer = nullptr;
    std::unique_ptr<SliceAnalyzer> m_analyzer = nullptr;

    std::unique_ptr<lve::LveCamera> m_camera;

    std::unique_ptr<wear::GrindingWheelWearCalculatorNew> m_wearCalculator;
    ParametricInstancedData m_parametricInstancedData;
    std::unique_ptr<SliceWearFitter> m_wearFitter;
};

}  // namespace slice