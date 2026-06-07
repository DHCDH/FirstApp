#pragma once

#include <cstdint>
#include <glm.hpp>
#include <vector>

namespace cpu
{

enum class GrindingWheelType
{
    PERIPHERY = 0,
};

// --- 1V1砂轮几何参数 ---
struct WheelParams {
    float gr1 = 0.3f;    // 圆角半径1 (mm)
    float gr2 = 0.3f;    // 圆角半径2 (mm)
    float ga;            // 斜角 (rad)
    float gR = 50.f;     // 砂轮最大半径 (mm)
    float width = 10.f;  // 砂轮宽度 (mm)
};

// --- 2D距离场图像 ---
struct SdfImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> data;  // row-major, 每个像素存储到最近容屑槽点的距离

    // 物理坐标映射参数（与GPU端shader_sdf_generate.comp一致）
    glm::vec3 targetPoint{0.f};  // 切片中心的世界坐标
    float xMin = 0.f;            // 像素x=0对应的物理Z偏移
    float zMax = 0.f;            // 像素y=0对应的物理Y偏移
    float dx = 0.f;              // X方向像素→物理缩放
    float dy = 0.f;              // Y方向像素→物理缩放

    float operator()(uint32_t row, uint32_t col) const
    {
        return data[row * width + col];
    }
    float& operator()(uint32_t row, uint32_t col)
    {
        return data[row * width + col];
    }

    // 通过UV坐标(0~1)采样距离值，双线性插值
    float Sample(float u, float v) const;

    bool IsValid() const
    {
        return !data.empty();
    }
};

// --- 切片平面定义 ---
struct SlicePlane {
    glm::vec3 normal{1.f, 0.f, 0.f};
    glm::vec3 point{0.f, 0.f, 0.f};
};

// --- 视图配置 ---
struct ViewConfig {
    float xMin = -50.f;
    float xMax = 50.f;
    float zMin = -50.f;
    float zMax = 50.f;
    uint32_t nX = 512;
    uint32_t nZ = 512;

    // 视口方向（相机看向的轴），默认沿X轴（即切片平面为YZ平面）
    glm::vec3 viewDir{1.f, 0.f, 0.f};
};

// --- 拟合搜索配置 ---
struct FitConfig {
    float grMin = 0.01f;
    float grMax = 1.5f;
    float tolerance = 0.001f;
    uint32_t maxIterations = 25;
};

// --- SDF生成参数 ---
struct SdfGenConfig {
    glm::vec3 targetPoint{0.f};
    float xMin = 0.f;
    float zMax = 0.f;
    float dx = 1.f;
    float dy = 1.f;
};

// --- 批量Loss评估结果 ---
struct LossResult {
    float x1;        // 候选半径1
    float x2;        // 候选半径2
    float loss1;     // 候选1的MSE loss
    float loss2;     // 候选2的MSE loss
};

}  // namespace cpu
