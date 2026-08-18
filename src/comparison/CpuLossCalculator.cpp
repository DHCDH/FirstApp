#include "CpuLossCalculator.h"

#include <algorithm>
#include <cmath>

#include "../Global.h"

namespace cpu
{

float CpuLossCalculator::ComputeLoss(const std::vector<glm::vec2>& points,
                                      const SdfImage& sdf) const
{
    if (points.empty() || !sdf.IsValid()) {
        return 0.f;
    }

    float totalLoss = 0.f;

    const float invDx = 1.f / sdf.dx;
    const float invDy = 1.f / sdf.dy;
    const float invW = 1.f / static_cast<float>(sdf.width);
    const float invH = 1.f / static_cast<float>(sdf.height);

    for (const auto& p : points) {
        float physY = p.x;
        float physZ = p.y;

        // 物理坐标 → 像素坐标（与SDF生成时的0.5偏移保持一致）
        float col = (physZ - sdf.targetPoint.z - sdf.xMin) * invDx - 0.5f;
        float row = (sdf.targetPoint.y + sdf.zMax - physY) * invDy - 0.5f;

        // 像素坐标 → UV
        float u = col * invW;
        float v = row * invH;

        if (u < 0.f || u > 1.f || v < 0.f || v > 1.f) {
            continue;
        }

        float dist = sdf.Sample(u, v);
        dist = std::min(dist, kMaxDistClamp);
        totalLoss += dist * dist * kLossScale;
    }

    return totalLoss;
}

}  // namespace cpu
