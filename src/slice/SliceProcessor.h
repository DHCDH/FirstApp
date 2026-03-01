#pragma once

#include <memory>

#include "../Global.h"
#include "LveDevice.h"
#include "SliceResourceContext.h"
#include "SliceRasterizer.h"
#include "SliceAnalyzer.h"

// 资源分配
class SliceProcessor
{
public:
    SliceProcessor(lve::LveDevice& device, uint32_t width, uint32_t height);
    ~SliceProcessor();

    // --- 核心调度流程 ---
    void ProcessFrame(VkCommandBuffer commandBuffer, const SliceFrameData& frameData,
                      const SliceViewConfig& viewConfig);

    // --- 数据输出 ---
    bool GetAnalysisResult(ResultData& result);
    const std::vector<glm::vec2>& GetContourPoints() const;

    // --- 获取离屏渲染图象 ---
    VkDescriptorImageInfo GetOutputImageInfo();

    // --- 获取计算完成同步信号 ---
    VkFence GetComputeFence() const;

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

private:
    void UpdateCameraUbo(const glm::vec3& normal, const glm::vec3& point,
                         const SliceViewConfig& viewConfig);

    lve::LveDevice& m_lveDevice;

    lve::LveModel* m_blankModel = nullptr;
    lve::LveModel* m_grndWheelModel = nullptr;
    glm::mat4 m_blankMatrix{1.f};
    std::vector<glm::mat4> m_grndWheelInstances;

    std::unique_ptr<SliceResourceContext> m_context = nullptr;
    std::unique_ptr<SliceRasterizer> m_rasterizer = nullptr;
    std::unique_ptr<SliceAnalyzer> m_analyzer = nullptr;

    std::unique_ptr<lve::LveCamera> m_camera;
};