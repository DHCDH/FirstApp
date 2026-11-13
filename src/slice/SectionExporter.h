#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm.hpp>

namespace slice
{
// 单个截面的导出单元
struct SectionExportSection
{
    glm::vec3 planePoint{0.f};
    glm::vec3 planeNormal{0.f, 0.f, 1.f};
    // 平面局部 2D 坐标，已按切线方向有序（对应 GPU 的 trace/align 结果）
    std::vector<glm::vec2> orderedPoints;
};

struct SectionExportOptions
{
    // 棒料半径 (mm)：OCC 侧用它构造闭合圆弧
    double blankRadius = 0.0;
    // 写进文件头的说明（刀轨名、仿真参数等），可空
    std::string source;
};

// 把多截面仿真结果导出成 OCC 可直接读取的文本文件
class SectionExporter
{
public:
    // 写到指定的文件路径（目录不存在会自动创建）；返回实际路径，失败返回空 path
    static std::filesystem::path Write(
        const std::filesystem::path& filePath, const SectionExportOptions& options,
        const std::vector<SectionExportSection>& sections);

    // 平面基：与 SliceProcessor::UpdateCameraUbo 里的相机基保持一致
    static void PlaneBasis(const glm::vec3& planeNormal, glm::vec3& u, glm::vec3& v);

    // 平面局部 2D -> 工件 3D（点一定落在该平面上）
    static glm::vec3 ToWorld(const glm::vec3& planePoint, const glm::vec3& u,
                             const glm::vec3& v, const glm::vec2& p);
};

}  // namespace slice
