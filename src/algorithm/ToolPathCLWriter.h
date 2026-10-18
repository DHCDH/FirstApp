#pragma once

#include <string>
#include <vector>

#include "../Global.h"

// 将刀轨(点 + 法向)写成 CL 文件。
// 进退刀策略与 lucCutterToolPathProcessor 一致:
//   每个切削段前插入 APPROACH + ENGAGE, 段后插入 RETRACT + DEPARTURE,
//   段与段之间插入 TRANSFER。
//   N1 G00 到首个 APPROACH 起点, 随后用 (type:N) 注释区分各运动段,
//   TRANSFER 用 G00, 其余用 G01。
class ToolPathCLWriter
{
public:
    enum MotionType
    {
        Rapid = 1,      // 快进
        Approach = 2,   // 逼近
        Engage = 3,     // 进刀
        StepOver = 4,   // 步进
        Cut = 5,        // 切削
        Transfer = 6,   // 移刀
        Retract = 7,    // 退刀
        Departure = 8,  // 离开
        SlowCut = 9,    // 慢速切削
    };

    struct Config
    {
        int toolNum = 0;            // 刀号: 头部 G(54+toolNum), T(toolNum+1)
        double width = 0.0;         // 砂轮宽度
        bool wheelClampOut = false; // 砂轮安装方向: true 时沿法向偏移 width 并反向法向
        int cutType = Cut;          // 输入切削段的类型, 写入 (type:N) 注释

        double safeDistance = 2.0;            // APPROACH/DEPARTURE 沿砂轮法向的安全距离
        double engagementDistance = 2.0;      // 插削进刀距离(ENGAGE)
        double disengagementDistance = 10.0;  // 退刀距离(RETRACT)
        double safePlane = 20.0;              // 初始快速定位距离(沿径向远离棒料轴), 应大于砂轮半径+棒料半径
        glm::vec3 barAxis{1.0f, 0.0f, 0.0f};  // 棒料轴线方向, 默认 X 轴
    };

    // 每个 ToolPath 对应一段(seg), 每段输出一行 (type:N) 注释。
    static bool Write(const std::vector<ToolPath>& toolpaths,
                      const Config& config,
                      const std::string& filename);

    // 解析 optimize 输出的刀轨 txt (new seg + x,y,z,nx,ny,nz), 得到多段刀轨。
    static bool LoadOptimizeToolPath(const std::string& filename,
                                     std::vector<ToolPath>& toolpaths);
};
