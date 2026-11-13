#include "SectionExporter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>

#include "Logger.h"

namespace slice
{
namespace
{
// 有符号面积（点列按顺序连成闭合多边形，末点自动接首点）
double SignedArea(const std::vector<glm::vec2>& pts)
{
    double area = 0.0;
    const size_t n = pts.size();
    if (n < 3) return 0.0;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2& a = pts[i];
        const glm::vec2& b = pts[(i + 1) % n];
        area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return 0.5 * area;
}

// 去掉相邻重复点，避免 OCC 侧生成零长度边
std::vector<glm::vec2> RemoveDuplicates(const std::vector<glm::vec2>& pts)
{
    std::vector<glm::vec2> out;
    out.reserve(pts.size());
    for (const glm::vec2& p : pts) {
        if (out.empty()) {
            out.push_back(p);
            continue;
        }
        const glm::vec2 d = p - out.back();
        if (glm::dot(d, d) > 1e-12f) {
            out.push_back(p);
        }
    }
    // 首尾重合也去掉（闭合由 OCC 的圆弧负责，不要在这里留重复点）
    if (out.size() > 2) {
        const glm::vec2 d = out.front() - out.back();
        if (glm::dot(d, d) <= 1e-12f) {
            out.pop_back();
        }
    }
    return out;
}

// CAD/工件坐标系与仿真坐标系的差异：YOZ 平面内关于 Y=Z 做一次镜像，
// 也就是交换 Y、Z 分量。对整个场景做这个变换是自洽的：
// 轴线沿 X 的圆柱（r 不变）、x=const 的截面、截面内以原点为心的圆都映射到自身，
// 所以平面点、法向、轮廓点一并变换后仍然互相匹配。
glm::vec3 ToCadConvention(const glm::vec3& v)
{
    return glm::vec3{v.x, v.z, v.y};
}
}  // namespace

void SectionExporter::PlaneBasis(const glm::vec3& planeNormal, glm::vec3& u, glm::vec3& v)
{
    // 与 SliceProcessor::UpdateCameraUbo 完全一致，否则点的位置会和屏幕对不上
    glm::vec3 w = planeNormal;
    if (glm::length(w) < 1e-8f) {
        w = glm::vec3(0.f, 0.f, 1.f);
    }
    w = glm::normalize(w);

    const glm::vec3 hintUp =
        (std::abs(glm::dot(w, glm::vec3{0.f, 1.f, 0.f})) > 0.99f)
            ? glm::vec3(0.f, 0.f, 1.f)
            : glm::vec3(0.f, 1.f, 0.f);

    u = glm::normalize(glm::cross(hintUp, w));
    v = glm::cross(w, u);
}

glm::vec3 SectionExporter::ToWorld(const glm::vec3& planePoint, const glm::vec3& u,
                                   const glm::vec3& v, const glm::vec2& p)
{
    return planePoint + u * p.x + v * p.y;
}

std::filesystem::path SectionExporter::Write(
    const std::filesystem::path& filePath, const SectionExportOptions& options,
    const std::vector<SectionExportSection>& sections)
{
    std::error_code ec;
    if (filePath.has_parent_path()) {
        std::filesystem::create_directories(filePath.parent_path(), ec);
    }

    const std::filesystem::path path = filePath;

    std::ofstream out(path);
    if (!out.is_open()) {
        ERROR("Failed to open section export: %s", path.string().c_str());
        return {};
    }
    out << std::fixed << std::setprecision(6);

    // ---- 文件头 ----
    out << "# gash_sections v1\n";
    out << "# units: mm\n";
    out << "# frame: workpiece/CAD; section circle center at origin\n";
    out << "# note: coordinates are mirrored about the plane Y=Z relative to the\n";
    out << "#       simulation frame (Y and Z swapped), to match the CAD convention\n";
    out << "# points: ordered along the cut wall (open curve). Close it in CAD with\n";
    out << "#         the arc of radius R centered at the origin, on the side that\n";
    out << "#         does not cross the removed region.\n";
    out << "# direction: first section normalized to CCW; every following section is\n";
    out << "#         chained so that its first point continues the previous one\n";
    out << "#         (start points form a continuous line for lofting)\n";
    if (!options.source.empty()) {
        out << "# source: " << options.source << "\n";
    }
    out << "R " << options.blankRadius << "\n";
    out << "N " << sections.size() << "\n";
    out << "# section <index> <flags> <px> <py> <pz> <nx> <ny> <nz> <npts>\n";
    out << "# flags: bit0 = has points\n";

    size_t written = 0;
    size_t reversedByChain = 0;

    // 起点链式归一化：GPU 的 trace 以"离原点最远的点"为起点，两个端点几乎
    // 等距，截面一转就会翻到另一端。这里强制每一面的起点都接着上一面，
    // 保证放样时顶点一一对应、不会扭。
    bool hasPrevStart = false;
    glm::vec2 prevStart{0.f};

    for (size_t i = 0; i < sections.size(); ++i) {
        const SectionExportSection& s = sections[i];

        std::vector<glm::vec2> pts = RemoveDuplicates(s.orderedPoints);

        if (pts.size() >= 3) {
            if (!hasPrevStart) {
                // 第一面：统一成逆时针，给出一个确定的初始方向
                if (SignedArea(pts) < 0.0) {
                    std::reverse(pts.begin(), pts.end());
                }
            } else {
                // 后面每一面：让起点接住上一面的起点，必要时整条反向
                const glm::vec2 dFront = pts.front() - prevStart;
                const glm::vec2 dBack = pts.back() - prevStart;
                if (glm::dot(dBack, dBack) < glm::dot(dFront, dFront)) {
                    std::reverse(pts.begin(), pts.end());
                    ++reversedByChain;
                }
            }
            prevStart = pts.front();
            hasPrevStart = true;
            ++written;
        }

        glm::vec3 u{1.f, 0.f, 0.f};
        glm::vec3 v{0.f, 1.f, 0.f};
        PlaneBasis(s.planeNormal, u, v);

        const glm::vec3 planePoint = ToCadConvention(s.planePoint);
        const glm::vec3 planeNormal = ToCadConvention(s.planeNormal);

        const unsigned flags = (pts.size() >= 3) ? 1u : 0u;
        out << "section " << i << ' ' << flags << ' ' << planePoint.x << ' '
            << planePoint.y << ' ' << planePoint.z << ' ' << planeNormal.x << ' '
            << planeNormal.y << ' ' << planeNormal.z << ' ' << pts.size() << '\n';

        for (const glm::vec2& p : pts) {
            const glm::vec3 w = ToCadConvention(ToWorld(s.planePoint, u, v, p));
            out << w.x << ' ' << w.y << ' ' << w.z << '\n';
        }
    }

    out.close();

    if (!out) {
        ERROR("Failed while writing section export: %s", path.string().c_str());
        return {};
    }

    INFO("Exported %zu/%zu section(s) to %s (start chain reversed %zu section(s))",
         written, sections.size(), path.string().c_str(), reversedByChain);

    std::error_code absEc;
    const std::filesystem::path absPath = std::filesystem::absolute(path, absEc);
    return absEc ? path : absPath;
}

}  // namespace slice
