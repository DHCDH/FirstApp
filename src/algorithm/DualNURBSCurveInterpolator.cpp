#include "DualNURBSCurveInterpolator.h"

#include <../tinynurbs/tinynurbs.h>
#include <../glm/glm.hpp>
#include <iostream>
#include <cassert>

ToolPath DualNURBSCurveInterpolator::Interpolate(const ToolPath& input, float stepLength)
{
    assert(input.size >= 4 && "invalid value: target - position");
    const int degree = 3;
    const double L = 1.;

    std::vector<glm::dvec3> tipPoints, axisPoints;
    double totalDist = 0.;
    for (size_t i = 0; i < input.size; i++) {
        glm::dvec3 p(input.points[i]);
        glm::dvec3 n(input.normals[i]);
        tipPoints.push_back(p);
        axisPoints.push_back(p + n * L);

        // 提前计算总弦长
        if (i > 0) {
            totalDist += glm::distance(input.points[i], input.points[i - 1]);
        }
    }

    std::vector<double> params = CalculateSharedParams(input);
    std::vector<double> knots = GenerateKnots(params, degree);

    std::vector<glm::dvec3> tipCtrlPnts =
        SolveControlPoints(tipPoints, params, knots, degree);
    std::vector<glm::dvec3> axisCtrlPnts =
        SolveControlPoints(axisPoints, params, knots, degree);

    tinynurbs::Curve3d tipCurve{degree, knots, tipCtrlPnts};
    tinynurbs::Curve3d axisCurve{degree, knots, axisCtrlPnts};

    ToolPath result;

    // 放弃使用导数的动态补偿，直接计算出精确的插值点数量
    int numPoints = std::max(2, static_cast<int>(std::ceil(totalDist / stepLength)));
    double du = 1.0 / static_cast<double>(numPoints - 1);

    for (int i = 0; i < numPoints; i++) {
        double u = i * du;
        if (u > 1.0) u = 1.0;  // 防止浮点数精度溢出

        glm::vec3 pos = tinynurbs::curvePoint(tipCurve, u);
        glm::vec3 axPos = tinynurbs::curvePoint(axisCurve, u);
        glm::vec3 norm = glm::normalize(axPos - pos);

        result.points.push_back(pos);
        result.normals.push_back(norm);
    }

    result.size = result.points.size();
    return result;
}

std::vector<double> DualNURBSCurveInterpolator::CalculateSharedParams(const ToolPath& path)
{
    std::vector<double> params;
    params.reserve(path.size);
    params.push_back(0.);
    double totalDist = 0.;
    for (size_t i = 1; i < path.size; i++) {
        totalDist += glm::distance(path.points[i], path.points[i - 1]);
        params.push_back(totalDist);
    }

    for (double& p : params) {
        p /= (totalDist > 0 ? totalDist : 1.f);
    }

    return params;
}

std::vector<double> DualNURBSCurveInterpolator::GenerateKnots(
    const std::vector<double>& params, int degree)
{
    int n = params.size();
    int m = n + degree + 1;
    std::vector<double> knots(m);

    for (int i = 0; i <= degree; i++) {
        knots[i] = 0.f;
    }

    for (int i = m - degree - 1; i < m; i++) {
        knots[i] = 1.f;
    }

    // 平均法生成中间节点
    for (int i = 1; i < n - degree; i++) {
        float sum = 0.f;
        for (int j = i; j < i + degree; j++) {
            sum += params[j];
        }
        knots[i + degree] = sum / degree;
    }
    return knots;
}

std::vector<glm::dvec3> DualNURBSCurveInterpolator::SolveControlPoints(
    const std::vector<glm::dvec3>& dataPoints,
    const std::vector<double>& params,
    const std::vector<double>& knots,
    int degree)
{
    int n = dataPoints.size();
    // 构造 A 矩阵 (n x n)
    std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0f));
    std::vector<glm::dvec3> P = dataPoints;

    for (int i = 0; i < n; ++i) {
        int span = tinynurbs::findSpan(degree, knots, params[i]);
        auto N = tinynurbs::bsplineBasis(degree, span, knots, params[i]);
        for (int j = 0; j <= degree; ++j) {
            A[i][span - degree + j] = N[j];
        }
    }

    // 高斯消元 (解 Ax = B)
    for (int i = 0; i < n; i++) {
        int pivot = i;
        for (int j = i + 1; j < n; j++) {
            if (std::abs(A[j][i]) > std::abs(A[pivot][i])) pivot = j;
        }
        std::swap(A[i], A[pivot]);
        std::swap(P[i], P[pivot]);

        for (int j = i + 1; j < n; j++) {
            double factor = A[j][i] / A[i][i];
            for (int k = i; k < n; k++) A[j][k] -= factor * A[i][k];
            P[j] -= factor * P[i];
        }
    }
    // 回代求解
    for (int i = n - 1; i >= 0; i--) {
        for (int j = i + 1; j < n; j++) P[i] -= A[i][j] * P[j];
        P[i] /= A[i][i];
    }
    return P;
}
