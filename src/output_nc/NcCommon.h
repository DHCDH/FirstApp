#pragma once

#include <glm.hpp>

#include <string>
#include <vector>

namespace nc {

// 工序类型，对应原 lucProcessStepType 中参与后置处理分支的子集。
// 枚举名与 .md 源码保持一致。
enum class OperationType
{
    FormRelief1,
    FormRelief2,
    NoseEndTipGashOut,
    TslotManualStepFaceCam,
    BarrelFlute,
    WoodToolFlute,
    Flute,
    TipGashOut,
    BallEndTipGashOut,
    ChamferGashOut,
    DrillClearance,
    Relief1,
    Relief2,
    TipRelief1,
    TipRelief2,
    BallEndTipClearance,
    BarrelRelief1,
    BarrelRelief2,
    Chamfer1,
    Chamfer2,
    Chamfer2V2,
    DrillGashOut,
    BallEndTipRelief1,
    BallEndTipRelief2,
    NoseEndTipRelief1,
    NoseEndTipRelief2,
    WoodToolFirstRelief,
    WoodToolSecondRelief,
    WoodToolRoughMill,
};

// 刀轨运动段类型，对应 LUCToolPathType。
enum class MotionType
{
    Undefined = 0,
    Rapid = 1,
    Approach = 2,
    Engage = 3,
    StepOver = 4,
    Cut = 5,
    Transfer = 6,
    Retract = 7,
    Departure = 8,
    SlowCut = 9,
};

enum class EngageMode
{
    Empty = 0,
    Cross = 1,
    Normal = 2,
    Axial = 3,
};

enum class RetractMode
{
    Empty = 0,
    Cross = 1,
    Normal = 2,
    Axial = 3,
};

// 输入刀轨段（点 + 法向）。
struct NcInputSeg
{
    std::vector<glm::dvec3> points;
    std::vector<glm::dvec3> normals;
};

// 经过处理器补齐进退刀/逼近/移刀后的运动段。
struct NcMotionSeg
{
    MotionType type = MotionType::Cut;
    std::vector<glm::dvec3> points;
    std::vector<glm::dvec3> normals;
};

// 处理器参数（对应 lucCutterToolPathProcessor）。
struct NcProcessorConfig
{
    OperationType operationType = OperationType::Flute;
    double safeDistance = 2.0;
    double engagementDistance = 2.0;
    double disengagementDistance = 10.0;
    int engagementDirection = 0;
};

// 写 NC 参数（对应 lucToolPathWriter）。
struct NcWriterConfig
{
    OperationType pathType = OperationType::Flute;  // 对应 PathType
    int toolNum = 0;
    double cylinderRate = 0.0;        // 切削进给率
    double engamentRate = 0.0;        // 进刀进给率
    double disengagementRate = 0.0;   // 退刀进给率
    double toolLength = 0.0;          // 刀具长度
    double fLangeDist = 0.0;          // FLangeDist 法兰长度
    double wheelPackageLength = 0.0;  // 砂轮组总长
    bool wheelClampOut = false;       // 砂轮安装方向（false=外, true=内）
    double clampingLength = 134.51;   // 工件夹紧外露长度
    double rs = 50.0;                 // 砂轮半径
    double lp = 0.0;                  // 砂轮端机床结构
    int mountingShaftIndex = 0;       // 安装轴序号（0 或 1）
};

struct NcWriteResult
{
    bool success = false;
    bool isOverTravel = false;
    std::string error;
};

const std::vector<OperationType>& AllOperationTypes();
const char* OperationTypeName(OperationType t);

// 解析刀轨 txt（new seg + x,y,z,nx,ny,nz）。
bool LoadToolPathTxt(const std::string& filename, std::vector<NcInputSeg>& segs);

// 处理器：给纯 CUT 刀轨补齐 ENGAGE/RETRACT/APPROACH/DEPARTURE/TRANSFER。
std::vector<NcMotionSeg> ProcessToolPath(const std::vector<NcInputSeg>& segs,
                                         const NcProcessorConfig& cfg);

// 写出 NC 文件（完整复刻 lucToolPathWriter 的后置处理）。
NcWriteResult WriteNc(const std::vector<NcMotionSeg>& motions,
                      const NcWriterConfig& cfg,
                      const std::string& filename);

}  // namespace nc
