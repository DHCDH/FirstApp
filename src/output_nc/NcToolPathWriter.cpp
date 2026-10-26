#include "NcCommon.h"

#include <gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace nc {

namespace {

constexpr double kPi = 3.14159265358979323846;

// 对应 lucNumberToString(Fixed, precision=4)
std::string Num(double v)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << v;
    return oss.str();
}

bool IsTipRelief(OperationType t)
{
    return t == OperationType::BallEndTipRelief1 ||
           t == OperationType::BallEndTipRelief2 ||
           t == OperationType::NoseEndTipRelief1 ||
           t == OperationType::NoseEndTipRelief2;
}

}  // namespace

NcWriteResult WriteNc(const std::vector<NcMotionSeg>& motions,
                      const NcWriterConfig& cfg,
                      const std::string& filename)
{
    NcWriteResult result;

    std::ofstream out(filename);
    if (!out.is_open()) {
        result.error = "无法打开输出文件: " + filename;
        return result;
    }

    const double Lp = cfg.lp;
    const double FLangeDist = cfg.fLangeDist;
    const double ClampingLength = cfg.clampingLength;
    const double WheelPackageLength = cfg.wheelPackageLength;
    const int MountingShaftIndex = cfg.mountingShaftIndex;

    // 头部（与 lucToolPathWriter 完全一致，注意 N4 被注释跳过）
    out << "N1 G00 G90 G54\n";
    out << "N2 G0 X" << Num(WheelPackageLength + Lp + ClampingLength) << "\n";
    out << "N3 G0 Y" << std::to_string(WheelPackageLength + 10)
        << " Z150 C0 B" << std::to_string(MountingShaftIndex * 180) << "\n";
    out << "N5 S4000 M04\n";
    out << "N6 M08\n";

    int lineNum = 6;
    double temp_C = 0.0;
    bool overTravel = false;

    const int nSeg = static_cast<int>(motions.size());
    for (int i = 0; i < nSeg; ++i) {
        const NcMotionSeg& seg = motions[i];

        // 段首 F 行
        std::string fLine = "N" + std::to_string(++lineNum);
        switch (seg.type) {
            case MotionType::Cut:
                fLine += " F" + std::to_string(static_cast<int>(cfg.cylinderRate));
                break;
            case MotionType::Engage:
                fLine += " F" + std::to_string(static_cast<int>(cfg.engamentRate));
                break;
            case MotionType::Retract:
                fLine += " F" + std::to_string(static_cast<int>(cfg.disengagementRate));
                break;
            case MotionType::Transfer:
            case MotionType::Departure:
            case MotionType::Approach:
                fLine += " F2000";
                break;
            default:
                fLine += " F150";
                break;
        }
        out << fLine << "\n";

        for (size_t j = 0; j < seg.points.size(); ++j) {
            const glm::dvec3 P = seg.points[j];
            glm::dvec3 N = j < seg.normals.size() ? seg.normals[j]
                                                  : glm::dvec3(0.0, 0.0, 0.0);

            // 根据砂轮安装方向反向砂轮法向
            if (cfg.wheelClampOut) {
                N = -N;
            }

            double A = std::atan2(-N.z, -N.y);
            double C = std::asin(-N.x);

            // 奇异区域
            if (cfg.pathType == OperationType::DrillClearance ||
                cfg.pathType == OperationType::WoodToolRoughMill) {
                A = std::atan2(-P.y, P.z);
            }
            if (std::abs(N.z) < 0.0001 && std::abs(N.y) < 0.0001) {
                A = std::atan2(-P.y, P.z);
            }

            const double L = Lp + FLangeDist;
            const double X = L * std::sin(C) - P.x + ClampingLength;
            const double Y = L * std::cos(C) - P.y * std::cos(A) - P.z * std::sin(A);
            const double Z = -P.y * std::sin(A) + P.z * std::cos(A);

            const double angelA = A * 180.0 / kPi;
            const double angelC = C * 180.0 / kPi;

            // 初始转轴角度
            if (i == 0 && j == 0 && std::abs(angelC) > 45) {
                // 复刻 lucTransform（PostMultiply）: M = T2 * Rz(-angelC) * T1
                const glm::dmat4 M =
                    glm::translate(glm::dmat4(1.0),
                                   glm::dvec3(-X + ClampingLength, -Y, Z)) *
                    glm::rotate(glm::dmat4(1.0),
                                -angelC * kPi / 180.0,
                                glm::dvec3(0.0, 0.0, 1.0)) *
                    glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, L, 0.0));
                const glm::dvec4 Pt = M * glm::dvec4(0.0, 0.0, 0.0, 1.0);
                if (Pt.y < 0.0) {
                    out << "N" << (++lineNum) << " G0 B"
                        << std::to_string(90 + MountingShaftIndex * 180) << "\n";
                }
            }

            const double shiftedC = angelC + MountingShaftIndex * 180;

            // 超程判断，软限位(X470,Y475,Z245,C-30,290)
            if (X >= 559.7435 || X <= 89.7435 || Y >= 282.674 || Y <= -192.326 ||
                Z >= 218.3455 || Z <= -27.6545 || shiftedC <= -30 || shiftedC >= 290) {
                overTravel = true;
                std::cout << "type:" << OperationTypeName(cfg.pathType) << "\n";
                std::cout << "OverTravel:" << lineNum << "\n";
            }

            std::string block;
            if (seg.type == MotionType::Transfer &&
                std::abs(C - temp_C) > 10.0 * kPi / 180.0 && IsTipRelief(cfg.pathType)) {
                block += "N" + std::to_string(++lineNum) + " G01" + " X" + Num(X) + "\n";
                block += "N" + std::to_string(++lineNum) + " G01" + " B" + Num(shiftedC) + "\n";
                block += "N" + std::to_string(++lineNum) + " G01" + " Y" + Num(Y) + " Z" +
                         Num(Z) + " C" + Num(angelA) + "\n";
            } else {
                block += "N" + std::to_string(++lineNum) + " G01" + " X" + Num(X) + " Y" +
                         Num(Y) + " Z" + Num(Z) + " C" + Num(angelA) + " B" + Num(shiftedC);
            }

            temp_C = C;
            out << block << "\n";
        }
    }

    // 尾部退刀回位
    out << "N" << (++lineNum) << " G0 X"
        << Num(WheelPackageLength + Lp + ClampingLength + 20) << "\n";
    out << "N" << (++lineNum) << " G0 Y250 Z150 C0 B"
        << std::to_string(MountingShaftIndex * 180) << "\n";

    out.close();

    result.success = true;
    result.isOverTravel = overTravel;
    return result;
}

}  // namespace nc
