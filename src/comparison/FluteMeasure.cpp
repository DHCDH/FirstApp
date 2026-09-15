#include "FluteMeasure.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace cpu
{

bool FluteMeasure::LoadPoints(const std::string& path, std::vector<glm::vec2>& points)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    points.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::stringstream ss(line);
        float x = 0.f, y = 0.f;
        if (ss >> x >> y) {
            points.emplace_back(x, y);
        }
    }
    return !points.empty();
}

FluteMeasureResult FluteMeasure::Measure(const std::vector<glm::vec2>& points)
{
    FluteMeasureResult result;
    const int n = static_cast<int>(points.size());
    if (n < 3) return result;

    // --- 芯厚半径：离原点最近的点 ---
    int coreIdx = 0;
    float minDistSq = 1e10f;
    for (int i = 0; i < n; ++i) {
        float d2 = glm::dot(points[i], points[i]);
        if (d2 < minDistSq) {
            minDistSq = d2;
            coreIdx = i;
        }
    }
    result.coreRadius = std::sqrt(minDistSq);

    // --- 槽宽角：首点与末点方向夹角 ---
    glm::vec2 tipVec = glm::normalize(points.front());
    glm::vec2 heelVec = glm::normalize(points.back());
    result.slotAngle =
        glm::degrees(std::acos(glm::clamp(glm::dot(tipVec, heelVec), -1.f, 1.f)));

    // --- 前角：对前刀面(首点→芯厚点)做二次拟合，取 t=0 处切线与内法向的夹角 ---
    int N = std::min(2500, coreIdx + 1);
    if (N < 3) return result;

    // 弦长参数化
    std::vector<float> t(N);
    t[0] = 0.f;
    float totalLen = 0.f;
    for (int i = 1; i < N; ++i) {
        totalLen += glm::distance(points[i], points[i - 1]);
        t[i] = totalLen;
    }
    if (totalLen > 1e-4f) {
        for (int i = 0; i < N; ++i) t[i] /= totalLen;
    }

    // 3x3 正规方程 (M^T M) X = M^T P，M = [1, t, t^2]
    float S0 = static_cast<float>(N);
    float S1 = 0.f, S2 = 0.f, S3 = 0.f, S4 = 0.f;
    float Rx0 = 0.f, Rx1 = 0.f, Rx2 = 0.f;
    float Ry0 = 0.f, Ry1 = 0.f, Ry2 = 0.f;

    for (int i = 0; i < N; ++i) {
        float ti = t[i];
        float t2 = ti * ti;
        float t3 = t2 * ti;
        float t4 = t3 * ti;
        S1 += ti;
        S2 += t2;
        S3 += t3;
        S4 += t4;

        float px = points[i].x;
        float py = points[i].y;
        Rx0 += px;
        Rx1 += px * ti;
        Rx2 += px * t2;
        Ry0 += py;
        Ry1 += py * ti;
        Ry2 += py * t2;
    }

    float det = S0 * (S2 * S4 - S3 * S3) - S1 * (S1 * S4 - S3 * S2) +
                S2 * (S1 * S3 - S2 * S2);
    if (std::abs(det) < 1e-6f) return result;

    // 伴随矩阵求逆，直接解出线性系数 B（即 t=0 处的切线）
    float A10 = S2 * S3 - S1 * S4;
    float A11 = S0 * S4 - S2 * S2;
    float A12 = S1 * S2 - S0 * S3;

    float bx = (A10 * Rx0 + A11 * Rx1 + A12 * Rx2) / det;
    float by = (A10 * Ry0 + A11 * Ry1 + A12 * Ry2) / det;

    glm::vec2 T = glm::normalize(glm::vec2(bx, by));
    glm::vec2 P0 = points.front();
    glm::vec2 R = glm::normalize(-P0);

    float crossVal = R.x * T.y - R.y * T.x;
    float dotVal = glm::dot(R, T);
    result.rakeAngle = glm::degrees(std::atan2(crossVal, dotVal));

    return result;
}

}  // namespace cpu
