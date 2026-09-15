#pragma once

#include <glm.hpp>
#include <vector>

namespace cpu
{
// 容屑槽点集后处理（对应 MATLAB 脚本 process_flute_pointset.m）
//
// 管线：缩放 + 镜像 → alpha-shape 提取外轮廓 → 边长剪断 → 首尾倒钩修剪 → 倒序
class ProcessFlutePoints
{
public:
    // 对容屑槽点集执行与 process_flute_pointset.m 相同的处理
    // points: CalculateFlute::CreateFlute() 的输出（仅使用 x, y）
    // 返回: 处理后的 2D 轮廓点（顺序与 MATLAB 输出一致）
    static std::vector<glm::vec2> Process(const std::vector<glm::vec3>& points);

    // 导出到 Cpu_comparison/cpu_process_flute.txt（格式: "x, y"）
    static void Export(const std::vector<glm::vec2>& points);
};

}  // namespace cpu
