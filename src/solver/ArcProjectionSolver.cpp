#include "ArcProjectionSolver.h"

#include <gtc/constants.hpp>
#include <iostream>
#include <exception>

#include "../Global.h"

constexpr double EPS_DBL = 1e-12;

using namespace glm;

ArcProjectionSolver::ArcProjectionSolver()
{
    // 先使用默认砂轮和刀具参数进行测试
}

std::vector<glm::mat4> ArcProjectionSolver::CalculateGrindingWheelPose()
{
    PROFILE_SCOPE("Arc projection");

    uint32_t numU1 = 100;
    uint32_t numU0c = 100;
    uint32_t numLambda = 100;

    double stepU1 = m_c.cuttingEdgeLength / static_cast<double>(numU1);
    //double stepU0c = m_gw.width / static_cast<double>(numU0c);
    // 只用大端圆圆角部分试切
    double stepU0c = m_gw.gr1 / static_cast<double>(numU0c);

    // --- 暂时仅用端面(u1 = 0)做测试
    for (int i = 0; i <= numU1; i++) {
        double u1 = i * stepU1;
        // 必须要保证sin(lambda) != 0
        double stepLambda = (glm::half_pi<double>() - m_c.helixAngle(u1)) / static_cast<double>(numLambda);

        for (int j = 1; j <= numLambda; j++) {
            double lambda = j * stepLambda;
            
            for (int k = 0; k <= numU0c; k++) {
                double u0c = k * stepU0c;
                NarrowGrindingWheelPosition(u0c, lambda, u1);
            }
        }

        break;
    }

    std::cout << "Transform matrixes vector size: " << m_transform.size() << "\n";

    return m_transform;
}

// Eq.19-23 计算符合前角和螺旋角的砂轮位置
void ArcProjectionSolver::NarrowGrindingWheelPosition(double u0c, double lambda,
                                                      double u1)
{
    double theta1 = CalculateTheta(u1);

    dvec4 r_t1 = CalculateCuttingEdgeCurve(u1, theta1);
    dvec4 n_t = CalculateCuttingEdgeCurveNormal(u1, theta1, r_t1);

    double r0 = m_gw.radius(u0c);
    double r0_du0 = m_gw.radiusDeriv(u0c);
    double c1 = sqrt(1. + r0_du0 * r0_du0);

    // Eq.21
    double xi = -(c1 * n_t.z - r0_du0 * cos(lambda)) / sin(lambda);
    if (xi > 1.) xi = 1.;
    if (xi < -1.) xi = -1.;
    
    // Eq.20
    double theta0c = glm::pi<double>() - asin(xi);

    // Eq.22, E1.23
    double numerator_sin =
        -c1 * (n_t.y * cos(theta0c) - n_t.x * cos(lambda) * sin(theta0c) -
                 r0_du0 * n_t.x * sin(lambda));
    double numerator_cos =
        -c1 * (n_t.x * cos(theta0c) + n_t.y * cos(lambda) * sin(theta0c) +
                 r0_du0 * n_t.y * sin(lambda));
    double denominator = cos(theta0c) * cos(theta0c) +
                         (cos(lambda) * sin(theta0c) + r0_du0 * sin(lambda)) *
                             (cos(lambda) * sin(theta0c) + r0_du0 * sin(lambda));
    if (fabs(denominator) < EPS_DBL) {
        denominator = (denominator >= 0 ? EPS_DBL : -EPS_DBL);
    }
    double sin_phi_t = numerator_sin / denominator;
    double cos_phi_t = numerator_cos / denominator;
    double phi_t = atan2(sin_phi_t, cos_phi_t);

    // Eq.19
    double a_x = r_t1.x * cos_phi_t + r_t1.y * sin_phi_t - r0 * cos(theta0c);
    double a_y = -r_t1.x * sin_phi_t + r_t1.y * cos_phi_t - r0 * cos(lambda) * sin(theta0c) +
          u0c * sin(lambda);
    double a_z = r_t1.z - u0c * cos(lambda) - r0 * sin(lambda) * sin(theta0c);

    // --- E1.16 --- 
    glm::mat4 Mtw;
    double cos_lam = cos(lambda);
    double sin_lam = sin(lambda);
    // 第 0 列
    Mtw[0][0] = cos_phi_t;
    Mtw[0][1] = sin_phi_t;
    Mtw[0][2] = 0.0f;
    Mtw[0][3] = 0.0f;

    // 第 1 列
    Mtw[1][0] = -sin_phi_t * cos_lam;
    Mtw[1][1] = cos_phi_t * cos_lam;
    Mtw[1][2] = sin_lam;
    Mtw[1][3] = 0.0f;

    // 第 2 列
    Mtw[2][0] = sin_phi_t * sin_lam;
    Mtw[2][1] = -cos_phi_t * sin_lam;
    Mtw[2][2] = cos_lam;
    Mtw[2][3] = 0.0f;

    // 第 3 列 (平移项，请仔细对照论文公式 16 最后的平移向量核对正负号)
    Mtw[3][0] = a_x * cos_phi_t - a_y * sin_phi_t;
    Mtw[3][1] = a_x * sin_phi_t + a_y * cos_phi_t;
    Mtw[3][2] = a_z;  // 这里注意对应 Mtt 的平移反向
    Mtw[3][3] = 1.0f;

    m_transform.push_back(Mtw);
}

// Eq.2，通过积分求解θ(u1)
double ArcProjectionSolver::CalculateTheta(double u1)
{
    if (u1 < 1e-10) return 0.;

    if (!m_integrator) {
        throw std::runtime_error("Integrator is not initialized.");
    }

    auto integrand = [this](double u) -> double {
        double dr = m_c.radiusDeriv(u);
        double r1 = m_c.radius(u);
        double beta = m_c.helixAngle(u);
        double denom = r1;
        // integrand = sqrt(dr^2 + 1) * tan(beta) / r1
        double val = sqrt(dr * dr + 1.0) * tan(beta) / std::max(denom, EPS_DBL);
        return val;
    };

    //integrator.SetStrategy(
    //    std::make_unique<AdaptiveSimpsonStrategy>(1e-10, 1e-10, 1e-2, 1e-12, 0.5));
    double theta = m_integrator->Integrate(integrand, 0, u1);

    return theta;
}

// Eq.3，计算切削刃曲线
dvec4 ArcProjectionSolver::CalculateCuttingEdgeCurve(double u1, double theta1)
{
    double r1 = m_c.radius(u1);
    double r_tx = r1 * cos(theta1);
    double r_ty = r1 * sin(theta1);
    double r_tz = u1;
    return {r_tx, r_ty, r_tz, 1.0};
}

// Eq.8 切削刃曲线单位法向量nt_1
dvec4 ArcProjectionSolver::CalculateCuttingEdgeCurveNormal(double u1, double theta1,
                                                           const glm::dvec4& r_t1)
{
    dvec4 b_t = CalculateRevolutionSurfaceNormal(u1, theta1);
    dvec4 t_t = CalculateCuttingEdgeCurveTangent(u1, theta1);
    dvec4 m_t = CalculateMt(b_t, t_t);
    double gamma_n = CalculateNormalRakeAngle(u1, r_t1, b_t, m_t);

    dvec3 v_b{b_t[0], b_t[1], b_t[2]};
    dvec3 v_t{t_t[0], t_t[1], t_t[2]};
    dvec3 v_m{m_t[0], m_t[1], m_t[2]};
    // 构造列主序矩阵
    dmat3 M = {v_b, v_t, v_m};

    dvec3 v{-std::sin(gamma_n), 0., std::cos(gamma_n)};

    dvec3 n = M * v;
    return {n.x, n.y, n.z, 0.};
}

// Eq.4 切削刃曲线单位切向量
glm::dvec4 ArcProjectionSolver::CalculateCuttingEdgeCurveTangent(double u1, double theta1)
{
    double r1 = m_c.radius(u1);
    double dr1_du1 = m_c.radiusDeriv(u1);
    double beta_rad = m_c.helixAngle(u1);
    double dtheta_du = 0.0;
    if (fabs(r1) > EPS_DBL) {
        dtheta_du = sqrt(dr1_du1 * dr1_du1 + 1.0) * std::tan(beta_rad) / r1;
    }
    double dr_tx = dr1_du1 * cos(theta1) - r1 * sin(theta1) * dtheta_du;
    double dr_ty = dr1_du1 * sin(theta1) + r1 * cos(theta1) * dtheta_du;
    double dr_tz = 1.0;
    dvec3 tang = glm::normalize(dvec3{dr_tx, dr_ty, dr_tz});
    dvec4 t_t = {tang[0], tang[1], tang[2], 0.};
    return t_t;
}

// Eq.5 & Eq.6 刀坯回转面单位法向量bt
dvec4 ArcProjectionSolver::CalculateRevolutionSurfaceNormal(double u1, double theta1)
{
    double dr1_du1 = m_c.radiusDeriv(u1);
    double denom = sqrt(1.0 + dr1_du1 * dr1_du1);
    double b_tx = cos(theta1) / denom;
    double b_ty = sin(theta1) / denom;
    double b_tz = -dr1_du1 / denom;
    return {b_tx, b_ty, b_tz, 0.0};
}

// Eq.7 m_t
glm::dvec4 ArcProjectionSolver::CalculateMt(const glm::dvec4& b_t, const glm::dvec4& t_t)
{
    dvec3 b{b_t[0], b_t[1], b_t[2]};
    dvec3 t{t_t[0], t_t[1], t_t[2]};
    dvec3 m = glm::cross(b, t);
    return {m[0], m[1], m[2], 0.0};
}

// Eq.9 计算法向前角
double ArcProjectionSolver::CalculateNormalRakeAngle(double u1, const glm::dvec4& r_t1,
                                                     const glm::dvec4& b_t,
                                                     const glm::dvec4& m_t)
{
    double gamma_r = m_c.radialRakeAngle(u1);

    double numerator = (m_t.x * r_t1.x + m_t.y * r_t1.y) * cos(gamma_r) +
                       (m_t.x * r_t1.y - m_t.y * r_t1.x) * sin(gamma_r);

    double denominator = (b_t.x * r_t1.x + b_t.y * r_t1.y) * cos(gamma_r) +
                         (b_t.x * r_t1.y - b_t.y * r_t1.x) * sin(gamma_r);

    if (fabs(denominator) < EPS_DBL) {
        std::cout << "ComputeNormalRakeAngle: error"
                  << "\n";
        return 0.0;
    }

    return std::atan(numerator / denominator);  // 注意象限？
}

ArcProjectionSolver ::~ArcProjectionSolver()
{
}