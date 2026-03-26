#pragma once

#include <iostream>
#include <chrono>
#include <iostream>
#include <string>

#include "lve/LveFrameInfo.h"
#include "lve/LveTextureManager.h"
#include "lve/lveSwapChain.h"

struct RenderContext {
    lve::LveDevice& device;
    std::unordered_map<uint32_t,
                       std::vector<std::array<VkDescriptorSet,
                                              lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>>>&
        submeshMatSets;
    std::unordered_map<uint32_t, std::vector<VkDescriptorSet>>& submeshTextureSets;
    std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>& dummyMatSets;
    lve::LveTextureManager& textureManager;
    lve::LveDescriptorSetLayout& materialParamSetLayout;
    lve::LveDescriptorPool& materialParamPool;
    std::vector<std::unique_ptr<lve::LveBuffer>>& materialParamBuffers;
    std::unordered_map<uint32_t, lve::MaterialGPU>& objectMaterialParams;
};

// 单个平面定义
struct Plane {
    glm::vec3 normal{1.f, 0.f, 0.f};    // 截平面法向
    glm::vec3 point{0.f, 0.f, 0.f};     // 截平面点
};

// 用于单模型绘制
struct SliceDrawInfo {
    VkCommandBuffer commandBuffer;
    lve::LveModel& model;
    const glm::mat4& modelMatrix;
    VkDescriptorSet globalDescriptorSet;
    glm::vec3 normal{1.f, 0.f, 0.f};
    glm::vec3 point{0.f, 0.f, 0.f};
};

// 用于实例化绘制
struct SliceInstancedInfo {
    VkCommandBuffer commandBuffer;
    lve::LveModel& model;
    VkBuffer instanceBuffer;
    uint32_t instanceCount;
    VkDescriptorSet globalDescriptorSet;
    glm::vec3 normal{1.f, 0.f, 0.f};
    glm::vec3 point{0.f, 0.f, 0.f};
};

// 用于平面全屏大三角绘制
struct SlicePlaneInfo {
    VkCommandBuffer commandBuffer;
    VkDescriptorSet globalDescriptorSet;
    glm::vec3 normal{1.f, 0.f, 0.f};
    glm::vec3 point{0.f, 0.f, 0.f};
};

// 定义每一帧的仿真物理状态
struct SliceFrameData {
    bool displayWireframe{true};

    Plane displayPlane{{1.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};

    glm::mat4 blankMatrix;                // 棒料的世界变换矩阵
    std::vector<glm::mat4> wheelMatrixes;  // 所有砂轮实例

    std::vector<Plane> planes;  // 所有需要离屏渲染的截面
};

// 用于计算
struct SliceComputeInfo {
    VkDescriptorSet descriptorSet;
    uint32_t width;
    uint32_t height;
    uint32_t maxPoints;
    uint32_t planeIndex;
    glm::vec3 normal{1.f, 0.f, 0.f};
    glm::vec3 point{0.f, 0.f, 0.f};
    glm::vec4 mapInfo;  // 映射参数：xMin, zMin, dx, dz。用于将像素坐标转换为世界坐标
};

// 光栅化数据
struct RasterizerData {
    lve::LveModel* blankModel = nullptr;
    lve::LveModel* grndWheelModel = nullptr;
    glm::mat4 blankMatrix{1.f};
    std::vector<glm::mat4> grndWheelInstances;
    //glm::vec3 point{0.f, 0.f, 0.f};
    //glm::vec3 normal{1.f, 0.f, 0.f};
};

// 定义图像分辨率和世界坐标视野
struct SliceViewConfig {
    float xMin, xMax;  // 世界坐标X范围
    float zMin, zMax;  // 世界坐标Z范围
    uint32_t nX, nZ;   // 图像分辨率
};

enum class ContactMaskCode : uint32_t
{
    EMPTY = 0u,           // 空
    BLANK_ONLY = 1u,      // 仅棒料
    GRNDWHEEL_ONLY = 2u,  // 仅砂轮
    INTERSECTION = 3u     // 棒料∩砂轮
};

enum class SliceDisplayMode : uint32_t
{
    SHOW_ALL = 0,
    BLANK_ONLY = 1,
    GRNDWHEEL_ONLY = 2,
    INTERSECTION_ONLY = 3
};

enum class RunningMode : uint32_t
{
    DISPLAY_ONLY = 0,
    DISPLAY_AND_ANALYZE = 1,
    OPTIMIZE = 2
};

struct ToolPath {
    size_t size{};
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> normals;
};

inline void PrintMat4(const glm::mat4& M, const std::string& name = "")
{
    std::cout << name << ": "
              << "\n";
    std::cout << M[0].x << " " << M[0].y << " " << M[0].z << " " << M[0].w << "\n";
    std::cout << M[1].x << " " << M[1].y << " " << M[1].z << " " << M[1].w << "\n";
    std::cout << M[2].x << " " << M[2].y << " " << M[2].z << " " << M[2].w << "\n";
    std::cout << M[3].x << " " << M[3].y << " " << M[3].z << " " << M[3].w << "\n";
}

inline void PrintVec3(const glm::vec3& v, const std::string& name = "")
{
    std::cout << name << "(";
    std::cout << v.x << " " << v.y << " " << v.z << ")\n";
}

class ScopedTimer
{
public:
    ScopedTimer(const std::string& name) : m_name(name)
    {
        m_start = std::chrono::high_resolution_clock::now();
    }

    ~ScopedTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto durationUs =
            std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
        std::cout << " [Profiler] " << m_name << " takes: " << durationUs / 1000.0
                  << " ms\n";
    }

private:
    std::string m_name;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)
#define PROFILE_SCOPE(name) ScopedTimer MACRO_CONCAT(timer, __LINE__)(name)

const uint32_t BATCH_LAYER_COUNT = 64;