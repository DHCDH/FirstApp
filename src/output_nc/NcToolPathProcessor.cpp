#include "NcCommon.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace nc {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Modes
{
    EngageMode engage = EngageMode::Cross;
    RetractMode retract = RetractMode::Cross;
};

// 复刻 lucCutterToolPathProcessor::RequestData 中按 OperationType 选择进退刀模式。
// 注意：原代码 lucSetClampMacro(EngageMode, EMPTY, NORMAL) 会把 AXIAL 钳制成 NORMAL。
Modes SelectModes(OperationType op)
{
    Modes m{EngageMode::Cross, RetractMode::Cross};

    if (op == OperationType::NoseEndTipGashOut ||
        op == OperationType::TslotManualStepFaceCam ||
        op == OperationType::BarrelFlute ||
        op == OperationType::WoodToolFlute ||
        op == OperationType::Flute ||
        op == OperationType::TipGashOut ||
        op == OperationType::BallEndTipGashOut ||
        op == OperationType::ChamferGashOut ||
        op == OperationType::DrillClearance) {
        m = {EngageMode::Cross, RetractMode::Cross};
    } else if (op == OperationType::Relief1 ||
               op == OperationType::Relief2 ||
               op == OperationType::TipRelief1 ||
               op == OperationType::TipRelief2 ||
               op == OperationType::BallEndTipClearance ||
               op == OperationType::BarrelRelief1 ||
               op == OperationType::BarrelRelief2) {
        m = {EngageMode::Normal, RetractMode::Axial};
    } else if (op == OperationType::Chamfer1 ||
               op == OperationType::Chamfer2 ||
               op == OperationType::Chamfer2V2) {
        m = {EngageMode::Normal, RetractMode::Normal};
    } else if (op == OperationType::DrillGashOut) {
        m = {EngageMode::Cross, RetractMode::Axial};
    } else if (op == OperationType::BallEndTipRelief1 ||
               op == OperationType::BallEndTipRelief2 ||
               op == OperationType::NoseEndTipRelief1 ||
               op == OperationType::NoseEndTipRelief2 ||
               op == OperationType::WoodToolFirstRelief ||
               op == OperationType::WoodToolSecondRelief) {
        m = {EngageMode::Axial, RetractMode::Axial};
    }

    if (static_cast<int>(m.engage) > static_cast<int>(EngageMode::Normal)) {
        m.engage = EngageMode::Normal;
    }
    return m;
}

glm::dvec3 SafeNormalize(glm::dvec3 v)
{
    double len = glm::length(v);
    return len > 1e-12 ? v / len : glm::dvec3(0.0, 0.0, 0.0);
}

glm::dvec3 ComputeEngagePoint(EngageMode mode,
                              const glm::dvec3& sp,
                              const glm::dvec3& snor,
                              const NcProcessorConfig& cfg)
{
    const double d = cfg.engagementDistance;

    if (cfg.engagementDirection != 0) {
        return sp + d * snor;
    }

    if (mode == EngageMode::Cross) {
        const glm::dvec3 axis(1.0, 0.0, 0.0);
        glm::dvec3 vN;
        if (std::abs(glm::dot(snor, axis)) > 0.9999) {
            vN = glm::dvec3(0.0, sp.y, sp.z);
        } else {
            vN = glm::cross(snor, axis);
        }
        vN = SafeNormalize(vN);
        glm::dvec3 pv(0.0, sp.y, sp.z);
        pv = SafeNormalize(pv);
        const double s = glm::dot(pv, vN) > 0.0 ? 1.0 : -1.0;
        return sp + s * d * vN;
    }

    // NORMAL（以及被钳制为 NORMAL 的 AXIAL）
    glm::dvec3 vN(1.0 - snor.x * snor.x, -snor.x * snor.y, -snor.x * snor.z);
    vN = SafeNormalize(vN);
    glm::dvec3 pv(0.0, sp.y, sp.z);
    pv = SafeNormalize(pv);
    if (glm::dot(pv, vN) < 0.0) {
        vN = -vN;
    }
    return sp + d * vN;
}

glm::dvec3 ComputeRetractPoint(RetractMode mode,
                               const glm::dvec3& ep,
                               const glm::dvec3& enor,
                               const glm::dvec3& snor,
                               const NcProcessorConfig& cfg)
{
    const double d = cfg.disengagementDistance;

    if (mode == RetractMode::Cross) {
        const glm::dvec3 axis(1.0, 0.0, 0.0);
        glm::dvec3 vN;
        if (std::abs(glm::dot(enor, axis)) > 0.9999) {
            vN = glm::dvec3(0.0, ep.y, ep.z);
        } else {
            vN = glm::cross(enor, axis);
        }
        vN = SafeNormalize(vN);
        glm::dvec3 pv(0.0, ep.y, ep.z);
        pv = SafeNormalize(pv);
        const double s = glm::dot(pv, vN) > 0.0 ? 1.0 : -1.0;
        return ep + s * d * vN;
    }

    if (mode == RetractMode::Axial) {
        return ep + d * enor;
    }

    // NORMAL：vNormal 用起点法向 snor，方向判断用终点 pv/enor
    glm::dvec3 vN(1.0 - snor.x * snor.x, -snor.x * snor.y, -snor.x * snor.z);
    vN = SafeNormalize(vN);
    glm::dvec3 pv(0.0, ep.y, ep.z);
    pv = SafeNormalize(pv);
    if (glm::dot(pv, enor) < 0.0) {
        return ep + d * vN;
    }
    return ep - d * vN;
}

}  // namespace

const std::vector<OperationType>& AllOperationTypes()
{
    static const std::vector<OperationType> all = {
        OperationType::FormRelief1,
        OperationType::FormRelief2,
        OperationType::NoseEndTipGashOut,
        OperationType::TslotManualStepFaceCam,
        OperationType::BarrelFlute,
        OperationType::WoodToolFlute,
        OperationType::Flute,
        OperationType::TipGashOut,
        OperationType::BallEndTipGashOut,
        OperationType::ChamferGashOut,
        OperationType::DrillClearance,
        OperationType::Relief1,
        OperationType::Relief2,
        OperationType::TipRelief1,
        OperationType::TipRelief2,
        OperationType::BallEndTipClearance,
        OperationType::BarrelRelief1,
        OperationType::BarrelRelief2,
        OperationType::Chamfer1,
        OperationType::Chamfer2,
        OperationType::Chamfer2V2,
        OperationType::DrillGashOut,
        OperationType::BallEndTipRelief1,
        OperationType::BallEndTipRelief2,
        OperationType::NoseEndTipRelief1,
        OperationType::NoseEndTipRelief2,
        OperationType::WoodToolFirstRelief,
        OperationType::WoodToolSecondRelief,
        OperationType::WoodToolRoughMill,
    };
    return all;
}

const char* OperationTypeName(OperationType t)
{
    switch (t) {
        case OperationType::FormRelief1: return "FormRelief1";
        case OperationType::FormRelief2: return "FormRelief2";
        case OperationType::NoseEndTipGashOut: return "NoseEndTipGashOut";
        case OperationType::TslotManualStepFaceCam: return "TslotManualStepFaceCam";
        case OperationType::BarrelFlute: return "BarrelFlute";
        case OperationType::WoodToolFlute: return "WoodToolFlute";
        case OperationType::Flute: return "Flute";
        case OperationType::TipGashOut: return "TipGashOut";
        case OperationType::BallEndTipGashOut: return "BallEndTipGashOut";
        case OperationType::ChamferGashOut: return "ChamferGashOut";
        case OperationType::DrillClearance: return "DrillClearance";
        case OperationType::Relief1: return "Relief1";
        case OperationType::Relief2: return "Relief2";
        case OperationType::TipRelief1: return "TipRelief1";
        case OperationType::TipRelief2: return "TipRelief2";
        case OperationType::BallEndTipClearance: return "BallEndTipClearance";
        case OperationType::BarrelRelief1: return "BarrelRelief1";
        case OperationType::BarrelRelief2: return "BarrelRelief2";
        case OperationType::Chamfer1: return "Chamfer1";
        case OperationType::Chamfer2: return "Chamfer2";
        case OperationType::Chamfer2V2: return "Chamfer2V2";
        case OperationType::DrillGashOut: return "DrillGashOut";
        case OperationType::BallEndTipRelief1: return "BallEndTipRelief1";
        case OperationType::BallEndTipRelief2: return "BallEndTipRelief2";
        case OperationType::NoseEndTipRelief1: return "NoseEndTipRelief1";
        case OperationType::NoseEndTipRelief2: return "NoseEndTipRelief2";
        case OperationType::WoodToolFirstRelief: return "WoodToolFirstRelief";
        case OperationType::WoodToolSecondRelief: return "WoodToolSecondRelief";
        case OperationType::WoodToolRoughMill: return "WoodToolRoughMill";
    }
    return "Unknown";
}

bool LoadToolPathTxt(const std::string& filename, std::vector<NcInputSeg>& segs)
{
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }

    segs.clear();
    NcInputSeg current;
    bool inSeg = false;

    auto flushSeg = [&]() {
        if (!current.points.empty() || !current.normals.empty()) {
            if (current.points.size() != current.normals.size()) {
                current = NcInputSeg{};
                return;
            }
            segs.emplace_back(std::move(current));
            current = NcInputSeg{};
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line == "new seg") {
            flushSeg();
            inSeg = true;
            continue;
        }

        if (!inSeg) continue;

        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);
        double x, y, z, nx, ny, nz;
        if (!(iss >> x >> y >> z >> nx >> ny >> nz)) {
            continue;
        }
        current.points.push_back({x, y, z});
        current.normals.push_back({nx, ny, nz});
    }

    flushSeg();
    return !segs.empty();
}

std::vector<NcMotionSeg> ProcessToolPath(const std::vector<NcInputSeg>& segs,
                                         const NcProcessorConfig& cfg)
{
    const Modes modes = SelectModes(cfg.operationType);
    const double safe = cfg.safeDistance;

    // ---- 阶段 1：给每个 CUT 段补 ENGAGE / RETRACT ----
    std::vector<NcMotionSeg> out1;
    for (const NcInputSeg& seg : segs) {
        if (seg.points.empty()) continue;

        glm::dvec3 sp = seg.points.front();
        const glm::dvec3 snor = seg.normals.front();
        const glm::dvec3 ep = seg.points.back();
        const glm::dvec3 enor = seg.normals.back();

        if (cfg.operationType == OperationType::FormRelief1 ||
            cfg.operationType == OperationType::FormRelief2) {
            sp.x -= 0.5;
        }

        // 输入文件每段都是 CUT，原算法中 addEngage / addRetract 恒为 true。
        {
            NcMotionSeg m;
            m.type = MotionType::Engage;
            m.points = {ComputeEngagePoint(modes.engage, sp, snor, cfg), sp};
            m.normals = {snor, snor};
            out1.push_back(std::move(m));
        }
        {
            NcMotionSeg m;
            m.type = MotionType::Cut;
            m.points = seg.points;
            m.normals = seg.normals;
            out1.push_back(std::move(m));
        }
        {
            NcMotionSeg m;
            m.type = MotionType::Retract;
            m.points = {ep, ComputeRetractPoint(modes.retract, ep, enor, snor, cfg)};
            m.normals = {enor, enor};
            out1.push_back(std::move(m));
        }
    }

    // ---- 阶段 2：在 RETRACT -> ENGAGE 之间补 DEPARTURE / TRANSFER / APPROACH ----
    std::vector<NcMotionSeg> out2;
    for (size_t i = 0; i < out1.size(); ++i) {
        const NcMotionSeg& cur = out1[i];
        out2.push_back(cur);

        if (i + 1 < out1.size()) {
            const NcMotionSeg& next = out1[i + 1];
            if (cur.type == MotionType::Retract && next.type == MotionType::Engage) {
                const glm::dvec3 curEp = cur.points.back();
                const glm::dvec3 curEnor = cur.normals.back();
                const glm::dvec3 nextSp = next.points.front();
                const glm::dvec3 nextSnor = next.normals.front();

                const glm::dvec3 curOutP = curEp + safe * curEnor;
                const glm::dvec3 nextInP = nextSp + safe * nextSnor;

                NcMotionSeg dep;
                dep.type = MotionType::Departure;
                dep.points = {curEp, curOutP};
                dep.normals = {curEnor, curEnor};
                out2.push_back(std::move(dep));

                NcMotionSeg tr;
                tr.type = MotionType::Transfer;
                tr.points = {curOutP, nextInP};
                tr.normals = {curEnor, nextSnor};
                out2.push_back(std::move(tr));

                NcMotionSeg app;
                app.type = MotionType::Approach;
                app.points = {nextInP, nextSp};
                app.normals = {nextSnor, nextSnor};
                out2.push_back(std::move(app));
            }
        }
    }

    // ---- 阶段 3：补首条 APPROACH 与末条 DEPARTURE ----
    std::vector<NcMotionSeg> out3;
    for (size_t i = 0; i < out2.size(); ++i) {
        const NcMotionSeg& cur = out2[i];

        if (i == 0 && cur.type != MotionType::Approach) {
            const glm::dvec3 curSp = cur.points.front();
            const glm::dvec3 curEnor = cur.normals.back();
            const glm::dvec3 curOutP = curSp + safe * curEnor;

            NcMotionSeg app;
            app.type = MotionType::Approach;
            app.points = {curOutP, curSp};
            app.normals = {curEnor, curEnor};
            out3.push_back(std::move(app));
        }

        out3.push_back(cur);

        if (i + 1 == out2.size() && cur.type != MotionType::Departure) {
            const glm::dvec3 curEp = cur.points.back();
            const glm::dvec3 curEnor = cur.normals.back();
            const glm::dvec3 curOutP = curEp + safe * curEnor;

            NcMotionSeg dep;
            dep.type = MotionType::Departure;
            dep.points = {curEp, curOutP};
            dep.normals = {curEnor, curEnor};
            out3.push_back(std::move(dep));
        }
    }

    return out3;
}

}  // namespace nc
