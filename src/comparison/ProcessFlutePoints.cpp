#include "ProcessFlutePoints.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace cpu
{

namespace
{

// ---- 与 process_flute_pointset.m 对齐的参数 ----
constexpr double kTargetR = 5.0;            // target_R
constexpr double kShrinkFactor = 0.8;       // boundary(x, y, 0.8)
constexpr double kBaseThreshold = 0.25;     // base_threshold
constexpr double kAngleThresholdDeg = 45.0; // angle_threshold
constexpr int kSearchRange = 10;            // search_range
constexpr double kPi = 3.14159265358979323846;

struct Point2D
{
    double x = 0.0;
    double y = 0.0;
    Point2D() = default;
    Point2D(double x_, double y_) : x(x_), y(y_) {}
};

double Dist2D(const Point2D& a, const Point2D& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// Delaunay 三角剖分 (Bowyer-Watson)
// ---------------------------------------------------------------------------
struct Tri
{
    int a, b, c;
};

// 保证三角形顶点按逆时针排列，使 in-circle 判定符号一致
Tri MakeCCW(const std::vector<Point2D>& pts, int a, int b, int c)
{
    const double cross = (pts[b].x - pts[a].x) * (pts[c].y - pts[a].y) -
                         (pts[b].y - pts[a].y) * (pts[c].x - pts[a].x);
    if (cross >= 0.0) return {a, b, c};
    return {a, c, b};
}

// 判断点 p 是否在三角形 t 的外接圆内（t 为逆时针时 det>0 表示在内）
bool InCircumcircle(const std::vector<Point2D>& pts, const Tri& t, const Point2D& p)
{
    const Point2D& a = pts[t.a];
    const Point2D& b = pts[t.b];
    const Point2D& c = pts[t.c];
    const double ax = a.x - p.x, ay = a.y - p.y;
    const double bx = b.x - p.x, by = b.y - p.y;
    const double cx = c.x - p.x, cy = c.y - p.y;
    const double det = (ax * ax + ay * ay) * (bx * cy - by * cx) -
                       (bx * bx + by * by) * (ax * cy - ay * cx) +
                       (cx * cx + cy * cy) * (ax * by - ay * bx);
    return det > 1e-12;
}

double CircumRadius(const std::vector<Point2D>& pts, const Tri& t)
{
    const Point2D& a = pts[t.a];
    const Point2D& b = pts[t.b];
    const Point2D& c = pts[t.c];
    const double ab = Dist2D(a, b);
    const double bc = Dist2D(b, c);
    const double ca = Dist2D(c, a);
    const double area2 =
        std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    if (area2 < 1e-12) return std::numeric_limits<double>::infinity();
    return (ab * bc * ca) / (2.0 * area2);  // R = abc / (4A)
}

std::pair<int, int> NormEdge(int a, int b)
{
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

std::vector<Tri> Delaunay(std::vector<Point2D>& pts)
{
    const int n = static_cast<int>(pts.size());
    if (n < 3) return {};

    double minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (const auto& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const double dx = maxX - minX;
    const double dy = maxY - minY;
    double dmax = std::max(dx, dy);
    if (dmax < 1e-9) dmax = 1.0;
    const double midX = (minX + maxX) / 2.0;
    const double midY = (minY + maxY) / 2.0;

    // 超级三角形（逆时针）
    pts.emplace_back(midX - 20.0 * dmax, midY - dmax);
    pts.emplace_back(midX + 20.0 * dmax, midY - dmax);
    pts.emplace_back(midX, midY + 20.0 * dmax);
    const int i1 = n, i2 = n + 1, i3 = n + 2;

    std::vector<Tri> tris;
    tris.push_back(MakeCCW(pts, i1, i2, i3));

    for (int i = 0; i < n; ++i) {
        std::vector<int> bad;
        for (int j = 0; j < static_cast<int>(tris.size()); ++j) {
            if (InCircumcircle(pts, tris[j], pts[i])) bad.push_back(j);
        }
        if (bad.empty()) continue;

        std::vector<std::pair<int, int>> edges;
        for (int j : bad) {
            edges.push_back(NormEdge(tris[j].a, tris[j].b));
            edges.push_back(NormEdge(tris[j].b, tris[j].c));
            edges.push_back(NormEdge(tris[j].c, tris[j].a));
        }
        std::sort(edges.begin(), edges.end());

        std::sort(bad.begin(), bad.end(), std::greater<int>());
        for (int j : bad) tris.erase(tris.begin() + j);

        // 只出现一次的边为空洞边界，与新点连成新三角形
        for (size_t k = 0; k < edges.size(); ++k) {
            int cnt = 1;
            while (k + 1 < edges.size() && edges[k + 1] == edges[k]) {
                ++cnt;
                ++k;
            }
            if (cnt == 1) {
                tris.push_back(MakeCCW(pts, edges[k].first, edges[k].second, i));
            }
        }
    }

    // 删除含超级三角形顶点的三角形
    tris.erase(std::remove_if(tris.begin(), tris.end(),
                              [&](const Tri& t) {
                                  return t.a >= n || t.b >= n || t.c >= n;
                              }),
               tris.end());

    pts.resize(n);
    return tris;
}

// ---------------------------------------------------------------------------
// alpha-shape 边界提取（等价于 MATLAB boundary(x, y, 0.8) 的外围轮廓）
// ---------------------------------------------------------------------------
std::vector<Point2D> AlphaShapeBoundary(const std::vector<Point2D>& pts, double alpha)
{
    std::vector<Point2D> work = pts;
    const std::vector<Tri> tris = Delaunay(work);
    if (tris.empty()) return pts;

    // 保留外接圆半径 <= alpha 的三角形，统计每条无向边出现次数
    std::vector<std::pair<int, int>> edges;
    for (const auto& t : tris) {
        if (CircumRadius(work, t) <= alpha) {
            edges.push_back(NormEdge(t.a, t.b));
            edges.push_back(NormEdge(t.b, t.c));
            edges.push_back(NormEdge(t.c, t.a));
        }
    }
    std::sort(edges.begin(), edges.end());

    // 只出现一次的边为边界边
    std::vector<std::pair<int, int>> boundaryEdges;
    for (size_t k = 0; k < edges.size(); ++k) {
        int cnt = 1;
        while (k + 1 < edges.size() && edges[k + 1] == edges[k]) {
            ++cnt;
            ++k;
        }
        if (cnt == 1) boundaryEdges.push_back(edges[k]);
    }
    if (boundaryEdges.empty()) return pts;

    // 邻接表 + 遍历成序列（闭环或开放链）
    std::map<int, std::vector<int>> adj;
    for (const auto& e : boundaryEdges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }

    // 起点优先选度数 1 的端点（开放链），否则任取（闭环）
    int start = adj.begin()->first;
    for (const auto& kv : adj) {
        if (kv.second.size() == 1) {
            start = kv.first;
            break;
        }
    }
    std::vector<int> seq{start};
    int prev = -1;
    int cur = start;
    while (true) {
        int next = -1;
        for (int nb : adj[cur]) {
            if (nb != prev) {
                next = nb;
                break;
            }
        }
        if (next == -1) break;  // 开放链末端
        if (next == start) {    // 闭环
            seq.push_back(start);
            break;
        }
        prev = cur;
        cur = next;
        seq.push_back(cur);
        if (static_cast<int>(seq.size()) > static_cast<int>(boundaryEdges.size()) + 2) {
            break;  // 防御死循环
        }
    }

    std::vector<Point2D> result;
    result.reserve(seq.size());
    for (int idx : seq) result.push_back(pts[idx]);
    return result;
}

// ---------------------------------------------------------------------------
// 边长剪断：断开过长的离散连线，保留最长连续段
// ---------------------------------------------------------------------------
std::vector<Point2D> SplitKeepLongest(const std::vector<Point2D>& poly, double threshold)
{
    const int N = static_cast<int>(poly.size());
    if (N < 2) return poly;

    const bool closed = Dist2D(poly.front(), poly.back()) < 1e-12;

    if (closed) {
        // 封闭多边形（与 MATLAB 一致）
        std::vector<int> breaks;
        for (int i = 0; i < N - 1; ++i) {
            if (Dist2D(poly[i], poly[i + 1]) > threshold) breaks.push_back(i);
        }
        if (breaks.empty()) {
            return std::vector<Point2D>(poly.begin(), poly.end() - 1);
        }

        const int shift = breaks[0];
        std::vector<Point2D> shifted;
        shifted.reserve(N - 1);
        for (int i = shift; i <= N - 2; ++i) shifted.push_back(poly[i]);
        for (int i = 0; i <= shift - 1; ++i) shifted.push_back(poly[i]);

        std::vector<int> invalid;
        for (int i = 0; i < static_cast<int>(shifted.size()) - 1; ++i) {
            if (Dist2D(shifted[i], shifted[i + 1]) > threshold) invalid.push_back(i);
        }

        std::vector<std::pair<int, int>> segs;
        int start = 0;
        for (int e : invalid) {
            segs.emplace_back(start, e);
            start = e + 1;
        }
        segs.emplace_back(start, static_cast<int>(shifted.size()) - 1);

        int best = 0;
        for (int i = 1; i < static_cast<int>(segs.size()); ++i) {
            if (segs[i].second - segs[i].first > segs[best].second - segs[best].first) {
                best = i;
            }
        }
        std::vector<Point2D> result;
        for (int i = segs[best].first; i <= segs[best].second; ++i) {
            result.push_back(shifted[i]);
        }
        return result;
    }

    // 开放链：直接按长边切段，取最长段
    std::vector<int> invalid;
    for (int i = 0; i < N - 1; ++i) {
        if (Dist2D(poly[i], poly[i + 1]) > threshold) invalid.push_back(i);
    }
    if (invalid.empty()) return poly;

    std::vector<std::pair<int, int>> segs;
    int start = 0;
    for (int e : invalid) {
        segs.emplace_back(start, e);
        start = e + 1;
    }
    segs.emplace_back(start, N - 1);

    int best = 0;
    for (int i = 1; i < static_cast<int>(segs.size()); ++i) {
        if (segs[i].second - segs[i].first > segs[best].second - segs[best].first) {
            best = i;
        }
    }
    std::vector<Point2D> result;
    for (int i = segs[best].first; i <= segs[best].second; ++i) {
        result.push_back(poly[i]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// 首尾倒钩修剪：删除端点处折返角度 > 阈值的毛刺
// ---------------------------------------------------------------------------
std::vector<Point2D> TrimBarbs(std::vector<Point2D> pts, double angleThreshDeg,
                               int searchRange)
{
    auto calcAngles = [](const std::vector<Point2D>& p) {
        std::vector<double> angles;
        angles.reserve(p.size() > 2 ? p.size() - 2 : 0);
        for (int i = 0; i + 2 < static_cast<int>(p.size()); ++i) {
            const Point2D u{p[i + 1].x - p[i].x, p[i + 1].y - p[i].y};
            const Point2D v{p[i + 2].x - p[i + 1].x, p[i + 2].y - p[i + 1].y};
            const double lu = std::hypot(u.x, u.y);
            const double lv = std::hypot(v.x, v.y);
            double a = 0.0;
            if (lu > 1e-12 && lv > 1e-12) {
                double d = (u.x * v.x + u.y * v.y) / (lu * lv);
                d = std::max(-1.0, std::min(1.0, d));
                a = std::acos(d) * 180.0 / kPi;
            }
            angles.push_back(a);
        }
        return angles;
    };

    if (static_cast<int>(pts.size()) <= 10) return pts;

    // 头部
    {
        const auto angles = calcAngles(pts);
        const int search = std::min(searchRange, static_cast<int>(angles.size()));
        int headIdx = -1;
        for (int i = 0; i < search; ++i) {
            if (angles[i] > angleThreshDeg) headIdx = i;
        }
        if (headIdx >= 0) pts.erase(pts.begin(), pts.begin() + headIdx + 1);
    }

    // 尾部（基于头部修剪后的点集）
    {
        const auto angles = calcAngles(pts);
        const int m = static_cast<int>(angles.size());
        if (m > 0) {
            const int tailStart = std::max(0, m - searchRange);
            int rel = -1;
            for (int i = tailStart; i < m; ++i) {
                if (angles[i] > angleThreshDeg) {
                    rel = i;
                    break;
                }
            }
            if (rel >= 0) {
                const int delFrom = rel + 2;
                if (delFrom < static_cast<int>(pts.size())) {
                    pts.erase(pts.begin() + delFrom, pts.end());
                }
            }
        }
    }

    return pts;
}

}  // namespace

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------
std::vector<glm::vec2> ProcessFlutePoints::Process(const std::vector<glm::vec3>& points)
{
    if (points.empty()) return {};

    // 步骤 0：半径精确缩放
    double maxR = 0.0;
    for (const auto& p : points) {
        maxR = std::max(maxR, std::sqrt(static_cast<double>(p.x) * p.x + p.y * p.y));
    }
    if (maxR < 1e-12) return {};
    const double scale = kTargetR / maxR;

    // 几何映射：x = -y_scaled, y = -x_scaled
    std::vector<Point2D> pts;
    pts.reserve(points.size());
    for (const auto& p : points) {
        const double xs = p.x * scale;
        const double ys = p.y * scale;
        pts.emplace_back(-ys, -xs);
    }

    // 步骤 1-2：boundary（alpha-shape 外轮廓）
    const double alpha = kTargetR / kShrinkFactor;
    std::vector<Point2D> boundary = AlphaShapeBoundary(pts, alpha);

    // 步骤 3：边长剪断
    const double threshold = kBaseThreshold * scale;
    std::vector<Point2D> final = SplitKeepLongest(boundary, threshold);

    // 步骤 4：首尾倒钩修剪
    final = TrimBarbs(final, kAngleThresholdDeg, kSearchRange);

    // 步骤 5：倒序
    std::reverse(final.begin(), final.end());

    std::vector<glm::vec2> result;
    result.reserve(final.size());
    for (const auto& p : final) {
        result.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    return result;
}

void ProcessFlutePoints::Export(const std::vector<glm::vec2>& points)
{
    const std::string dirPath = "D:/Data/Study/vulkan/FirstApp/output_stuff/wear/Cpu_comparison";
    const std::string filePath = dirPath + "/cpu_process_flute.txt";

    std::filesystem::create_directories(dirPath);

    std::ofstream file(filePath);
    if (!file.is_open()) return;

    for (const auto& p : points) {
        file << p.x << ", " << p.y << '\n';
    }
    file.close();
}

}  // namespace cpu
