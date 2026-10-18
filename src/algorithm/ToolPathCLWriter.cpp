#include "ToolPathCLWriter.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

bool ToolPathCLWriter::Write(const std::vector<ToolPath>& toolpaths,
                             const Config& config,
                             const std::string& filename)
{
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return false;
    }

    // 与 lucNumberToString(Fixed, precision=4) 一致: 定点表示, 保留 4 位小数
    out << std::fixed << std::setprecision(4);

    glm::vec3 barAxis = glm::length(config.barAxis) > 1e-6f
                            ? glm::normalize(config.barAxis)
                            : glm::vec3(1.0f, 0.0f, 0.0f);

    // 对刀点变换: 砂轮安装方向偏移 + 法向反向
    auto transform = [&](const glm::vec3& p, const glm::vec3& n) {
        glm::vec3 P = p;
        glm::vec3 N = n;
        if (config.wheelClampOut) {
            P += static_cast<float>(config.width) * N;
            N = -N;
        }
        return std::pair<glm::vec3, glm::vec3>{P, N};
    };

    // 插削方向: 法向与棒料轴不平行时取 nor×barAxis, 平行时退化为径向
    auto crossDir = [&](const glm::vec3& nor, const glm::vec3& p) {
        glm::vec3 v;
        if (std::abs(glm::dot(nor, barAxis)) > 0.9999f) {
            v = glm::vec3(0.0f, p.y, p.z);
        } else {
            v = glm::cross(nor, barAxis);
        }
        float len = glm::length(v);
        return len > 1e-6f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
    };

    auto radial = [](const glm::vec3& p) {
        glm::vec3 v(0.0f, p.y, p.z);
        float len = glm::length(v);
        return len > 1e-6f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
    };

    // 第一步: 计算每个输入切削段的进退刀几何
    struct SegGeom
    {
        std::vector<glm::vec3> pts;
        std::vector<glm::vec3> nors;
        glm::vec3 sp, snor, ep, enor;  // 首末点及其法向
        glm::vec3 lineP;               // 进刀点(插削)
        glm::vec3 linePR;              // 退刀点
        glm::vec3 home;                // 初始安全定位点(径向远离棒料)
        glm::vec3 D1;                  // DEPARTURE 终点
    };
    std::vector<SegGeom> geoms;

    for (const ToolPath& seg : toolpaths) {
        if (seg.points.empty()) continue;

        SegGeom g;
        for (size_t j = 0; j < seg.points.size(); ++j) {
            glm::vec3 n = j < seg.normals.size() ? seg.normals[j] : glm::vec3(0.0f);
            auto [pp, nn] = transform(seg.points[j], n);
            g.pts.push_back(pp);
            g.nors.push_back(nn);
        }
        g.sp = g.pts.front();
        g.snor = g.nors.front();
        g.ep = g.pts.back();
        g.enor = g.nors.back();

        float safe = static_cast<float>(config.safeDistance);

        // 进刀: snor 与棒料轴叉乘得到插削方向, 再用径向点积定符号
        glm::vec3 vN = crossDir(g.snor, g.sp);
        glm::vec3 pv = radial(g.sp);
        float engSign = glm::dot(pv, vN) > 0.0f ? 1.0f : -1.0f;
        g.lineP =
            g.sp + engSign * static_cast<float>(config.engagementDistance) * vN;
        g.home = g.sp + static_cast<float>(config.safePlane) * radial(g.sp);

        // 退刀
        glm::vec3 vNr = crossDir(g.enor, g.ep);
        glm::vec3 pvr = radial(g.ep);
        float retSign = glm::dot(pvr, vNr) > 0.0f ? 1.0f : -1.0f;
        g.linePR =
            g.ep + retSign * static_cast<float>(config.disengagementDistance) * vNr;
        g.D1 = g.linePR + safe * g.enor;

        geoms.push_back(std::move(g));
    }

    if (geoms.empty()) {
        out.close();
        return true;
    }

    // 第二步: 组装运动段
    struct MotionSeg
    {
        int type;
        std::vector<glm::vec3> pts;
        std::vector<glm::vec3> nors;
    };
    std::vector<MotionSeg> motions;

    for (size_t i = 0; i < geoms.size(); ++i) {
        const SegGeom& g = geoms[i];
        if (i == 0) {
            motions.push_back({Approach, {g.home, g.lineP}, {g.snor, g.snor}});
        } else {
            const SegGeom& prev = geoms[i - 1];
            motions.push_back({Transfer, {prev.D1, g.home}, {prev.enor, g.snor}});
            motions.push_back({Approach, {g.home, g.lineP}, {g.snor, g.snor}});
        }
        motions.push_back({Engage, {g.lineP, g.sp}, {g.snor, g.snor}});
        motions.push_back({config.cutType, g.pts, g.nors});
        motions.push_back({Retract, {g.ep, g.linePR}, {g.enor, g.enor}});
        motions.push_back({Departure, {g.linePR, g.D1}, {g.enor, g.enor}});
    }

    // 第三步: 写文件
    const glm::vec3& first = motions.front().pts.front();    // 首个 APPROACH 起点
    const glm::vec3& firstNor = motions.front().nors.front();
    out << "N1 G00 G90 G" << (54 + config.toolNum)
        << " X" << first.x << " Y" << first.y << " Z" << first.z
        << " I" << firstNor.x << " J" << firstNor.y << " K" << firstNor.z
        << "\n";

    out << "N2 S500 M03 M08 T" << (config.toolNum + 1)
        << " M06 I1 J0 K0 F150"
        << "\n";

    int lineNum = 2;
    for (const MotionSeg& m : motions) {
        out << "(type:" << m.type << ")\n";
        const char* gcode = (m.type == Transfer) ? "G00" : "G01";
        for (size_t j = 0; j < m.pts.size(); ++j) {
            const glm::vec3& P = m.pts[j];
            const glm::vec3& N = j < m.nors.size() ? m.nors[j] : glm::vec3(0.0f);
            out << "N" << (lineNum + 1) << " " << gcode
                << " X" << P.x << " Y" << P.y << " Z" << P.z
                << " I" << N.x << " J" << N.y << " K" << N.z
                << "\n";
            ++lineNum;
        }
    }

    out.close();
    std::cout << "[System] Successfully exported CL file to: " << filename << "\n";
    return true;
}

bool ToolPathCLWriter::LoadOptimizeToolPath(const std::string& filename,
                                            std::vector<ToolPath>& toolpaths)
{
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return false;
    }

    toolpaths.clear();

    ToolPath current;
    bool inSeg = false;  // 读取到 new seg 才开始收集

    auto flushSeg = [&]() {
        if (!current.points.empty() || !current.normals.empty()) {
            if (current.points.size() != current.normals.size()) {
                std::cerr << "points/normals size mismatch in a segment\n";
                current = ToolPath{};
                return;
            }

            current.size = current.points.size();
            toolpaths.emplace_back(std::move(current));
            current = ToolPath{};
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

        float x, y, z, nx, ny, nz;
        if (!(iss >> x >> y >> z >> nx >> ny >> nz)) {
            std::cerr << "Warning: bad data line (skip): " << line << "\n";
            continue;
        }

        current.points.push_back({x, y, z});
        current.normals.push_back({nx, ny, nz});
    }

    flushSeg();
    return !toolpaths.empty();
}
