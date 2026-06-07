#include "CalculateFlute.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

#include "CalculateGrindingWheelMaxRadius.h"
#include "Logger.h"

static const float M_PI = glm::pi<float>();
static const float M_PI_2 = glm::pi<float>() / 2.f;

using range_ = std::pair<float, float>;

static const float R = 5.;
static const float beta = 30.f;
static const float lead = M_PI * R * 2 / tan(glm::radians(beta));

namespace cpu
{
std::vector<glm::vec3> CalculateFlute::CreateFlute()
{
    PROFILE_SCOPE("CalculateFlute::CreateFlute");
    InitGrindingWheelEquation();

    // 获取砂轮位姿矩阵
    glm::mat4 M = CalculateGrindingWheelPose();

    std::vector<glm::vec3> pSet = CalculateFlutePointSet(M);
    pSet = ProcessPointSet(pSet);

    ExportFlutePoints(pSet);

    return pSet;
}

void CalculateFlute::InitGrindingWheelEquation()
{
    PROFILE_SCOPE("CalculateFlute::InitGrindingWheelEquation");
    float gR =
        CalculateGrindingWheelMaxRadius::Calculate1V1GrindingWheelMaxRadius(m_gp.gR,
                                                                            m_gp.gr1,
                                                                            m_gp.ga);
    float gr1 = m_gp.gr1;
    float gr2 = m_gp.gr2;
    float gb = m_gp.width;
    float ga = m_gp.ga;

    INFO("gR: %f, gr1: %f, gb: %f, ga: %f", gR, gr1, gb, ga);

    switch (m_grndWheelType) {
        case GrindingWheelType::PERIPHERY: {
            /*平砂轮*/
            std::cout << "---=PERIPHERY=--" << std::endl;
            if (ga == M_PI_2) {
                /*1A1*/
                std::cout << "---=1A1=--" << std::endl;
                m_gRanges = {range_(0, gr1), range_(gr1, gb - gr1), range_(gb - gr1, gb)};
                auto fg12_expr = [=](const float& m1) {
                    return gR - gr1 + sqrt(gr1 * gr1 - (gr1 - m1) * (gr1 - m1));
                };
                auto fg23_expr = [=](const float& m1) { return gR; };
                auto fg34_expr = [=](const float& m1) {
                    return gR - gr2 +
                           sqrt(gr2 * gr2 - (m1 - (gb - gr2)) * (m1 - (gb - gr2)));
                };

                m_gEquations.push_back(fg12_expr);
                m_gEquations.push_back(fg23_expr);
                m_gEquations.push_back(fg34_expr);

            } else if (ga > 0 && M_PI_2 - ga > 1e-2) {
                /*1V1*/
                std::cout << "---=1V1=--" << std::endl;
                m_gRanges = {range_(0, gr1 + gr1 * cos(ga)),
                             range_(gr1 + gr1 * cos(ga), gb - (gr2 - gr2 * cos(ga))),
                             range_(gb - (gr2 - gr2 * cos(ga)), gb)};
                auto fg12_expr = [=](const float& m1) {
                    return gR - gr1 + sqrt(gr1 * gr1 - (gr1 - m1) * (gr1 - m1));
                };
                auto fg23_expr = [=](const float& m1) {
                    return gR - gr1 + gr1 * sin(ga) -
                           (m1 - (gr1 + gr1 * cos(ga))) * (1 / tan(ga));
                };
                auto fg34_expr = [=](const float& m1) {
                    return gR - gr1 * (1 - 1 / tan(ga / 2)) - gb / tan(ga) -
                           gr2 * tan(ga / 2) +
                           sqrt(gr2 * gr2 - (gr2 - gb + m1) * (gr2 - gb + m1));
                };

                m_gEquations.push_back(fg12_expr);
                m_gEquations.push_back(fg23_expr);
                m_gEquations.push_back(fg34_expr);
            }
            break;
        }
        default:
            break;
    }

    return;
}

glm::mat4 CalculateFlute::CalculateGrindingWheelPose()
{
    PROFILE_SCOPE("CalculateFlute::CalculateGrindingWheelPose");
    glm::mat4 matrix;

    glm::vec3 p_tmp = m_toolpaths[0].points[0];
    glm::vec3 point{p_tmp.y, p_tmp.z, p_tmp.x};

    glm::vec3 norm_tmp = m_toolpaths[0].normals[0];
    glm::vec3 norm{norm_tmp.y, norm_tmp.z, norm_tmp.x};

    point = glm::vec3(41.6311, 32.6075, -0.9441);
    norm = glm::vec3(0.4319, -0.6301, 0.6453);

    INFO("p(%f, %f, %f), n(%f, %f, %f)",
         point.x,
         point.y,
         point.z,
         norm.x,
         norm.y,
         norm.z);

    matrix = CalculateMatrix(norm, point);

    return matrix;
}

glm::mat4 CalculateFlute::CalculateMatrix(const glm::vec3& normal, const glm::vec3& point)
{
    PROFILE_SCOPE("CalculateFlute::CalculateMatrix");
    glm::vec3 DY{0.f, 0.f, 1.f};
    glm::vec3 axis{};
    axis = glm::cross(DY, normal);

    float lambda = std::acos(glm::dot(DY, normal));
    //DEBUG("lambda = %f rad (%f deg)", lambda, glm::degrees(lambda));
    axis = glm::normalize(axis);
    //DEBUG("axis = (%f, %f, %f)", axis.x, axis.y, axis.z);

    const glm::mat4 K{
        glm::vec4(0.0f, axis.z, -axis.y, 0.0f),  // column 0
        glm::vec4(-axis.z, 0.0f, axis.x, 0.0f),  // column 1
        glm::vec4(axis.y, -axis.x, 0.0f, 0.0f),  // column 2
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)        // column 3 (homogeneous)
    };
    // Print rows (matching original row-major display)
    //for (int row = 0; row < 4; ++row) {
    //    DEBUG("K | % f % f % f % f |", K[0][row], K[1][row], K[2][row], K[3][row]);
    //}

    const glm::mat4 I(1.0f);
    const float s = std::sin(lambda);
    const float c1 = 1.0f - std::cos(lambda);

    glm::mat4 R = I + s * K + c1 * (K * K);

    R[3] = glm::vec4(point[0], point[1], point[2], 1.0f);

    //for (int row = 0; row < 4; ++row) {
    //    DEBUG("R | % f % f % f % f |", R[0][row], R[1][row], R[2][row], R[3][row]);
    //}

    return R;
}

std::vector<glm::vec3> CalculateFlute::CalculateFlutePointSet(const glm::mat4& M)
{
    PROFILE_SCOPE("CalculateFlute::CalculateFlutePointSet");
    float T = 0.5 * M_PI;
    std::mutex mtx_t;

    auto calculate_points_t = [&, this](std::vector<glm::vec3>& coordVec,
                                        const float z,
                                        const int k,
                                        const float& deltaT) {
        float t = k * deltaT;
        std::vector<glm::vec3> coordVecTmp;
        for (int j = 0; j < m_gEquations.size(); j++) {
            float M1 = m_gRanges[j].second;
            // float deltaM1 = 0.05;
            float deltaM1 = 0.01;  // mark
            for (float m1 = m_gRanges[j].first; m1 < M1 + deltaM1; m1 += deltaM1) {
                if (m1 > M1) m1 = M1;
                float result[3] = {0.};
                glm::vec3 coord = SolveNewton2D(m1, t, z, m_gEquations[j](m1), M);
                if (coord.z == 0) {
                    // std::lock_guard<std::mutex> lock(mtx_t);
                    // coordVec.emplace_back(coord);
                    coordVecTmp.emplace_back(coord);
                }
            }
        }
        std::lock_guard<std::mutex> lock(mtx_t);
        coordVec.insert(coordVec.end(), coordVecTmp.begin(), coordVecTmp.end());
    };

    float z = 0.f;
    std::vector<glm::vec3> pSet;

    std::counting_semaphore<16> tSemaphore(16);
    std::vector<std::thread> threadsT;
    // Standard_Integer nT = std::abs(T / 0.01);
    int nT = std::abs(T / 0.001);  // mark
    // if (m_rBlade > 10) {
    //    nT = std::abs(T / 0.002);
    //}
    float deltaT = T / nT;

    for (int k = -nT; k <= nT; k++) {
        if (tSemaphore.try_acquire_for(std::chrono::seconds(5))) {
            threadsT.push_back(std::thread([&, z, k, deltaT]() {
                try {
                    calculate_points_t(pSet, z, k, deltaT);
                } catch (const std::exception& e) {
                    std::cerr << "Exception caught: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception caught" << std::endl;
                }
                tSemaphore.release();
            }));
        } else {
            std::cerr << "Timeout waiting for semaphore" << std::endl;
        }
    }

    for (auto& th : threadsT) {
        th.join();
    }

    return pSet;
}

glm::vec3 CalculateFlute::SolveNewton2D(float m1, float t, float z, float fg,
                                        const glm::mat4& mat)
{
    const float A00 = mat[0][0], A01 = mat[1][0], A02 = mat[2][0], A03 = mat[3][0];
    const float A10 = mat[0][1], A11 = mat[1][1], A12 = mat[2][1], A13 = mat[3][1];
    const float A20 = mat[0][2], A21 = mat[1][2], A22 = mat[2][2], A23 = mat[3][2];

    double lambda = 0.0;

    auto mxQt = [&](double m2) -> double {
        return std::cos(t - lambda) *
                   (A03 + A02 * m1 + A00 * fg * std::cos(m2) + A01 * fg * std::sin(m2)) -
               std::sin(t - lambda) *
                   (A13 + A12 * m1 + A10 * fg * std::cos(m2) + A11 * fg * std::sin(m2));
    };

    auto myQt = [&](double m2) -> double {
        return std::cos(t - lambda) *
                   (A13 + A12 * m1 + A10 * fg * std::cos(m2) + A11 * fg * std::sin(m2)) +
               std::sin(t - lambda) *
                   (A03 + A02 * m1 + A00 * fg * std::cos(m2) + A01 * fg * std::sin(m2));
    };

    auto F = [&](double m2) -> double {
        return A23 + A22 * m1 + A20 * fg * std::cos(m2) + A21 * fg * std::sin(m2) +
               (lead * (t - lambda)) / (2.0 * M_PI) - z;
    };

    auto dF_dm2 = [F](double m2) -> double {
        double h = 1e-4;
        return (F(m2 + h) - F(m2 - h)) / (2 * h);
    };

    const double epsilon = 1e-7;
    const float M2 = 2 * M_PI;
    const float deltaM2 = M2 / 10;
    std::array<float, 6> m2Array = {1., 2., 3., 4., 5., 6.};

    for (int i = 0; i < m2Array.size(); i++) {
        for (double m2_ = m2Array[i]; m2_ <= M2; m2_ += deltaM2) {
            double m2 = m2_;
            // 牛顿法迭代求解
            int iterationCount = 0;
            while (iterationCount < 100) {
                double fVal = F(m2);
                double df = dF_dm2(m2);

                if (std::abs(df) < 1e-10) {
                    break;
                }

                if (std::abs(fVal) < epsilon) {
                    // 已经接近解，返回结果
                    double x = mxQt(m2);
                    double y = myQt(m2);

                    if (x * x + y * y <= R * R) {
                        return {x, y, z};
                    }

                    break;
                }

                // 更新 m2 的值
                m2 -= fVal / df;
                iterationCount++;
            }
        }
    }
    //没有找到合适的解
    return {-1., -1., -1.};
}

std::vector<glm::vec3> CalculateFlute::ProcessPointSet(std::vector<glm::vec3> pSet)
{
    PROFILE_SCOPE("CalculateFlute::ProcessPointSet");
    if (pSet.size() <= 2) return pSet;

    glm::vec3 pO{0.f, 0.f, 0.f};
    int number = R * 30;  //圆环数量
    float delta_mR = R / number;
    std::vector<std::vector<glm::vec3>> ring(number + 1);

    //分配圆环
    for (int i = 0; i < pSet.size(); i++) {
        float x = pSet[i].x;
        float y = pSet[i].y;
        float z = pSet[i].z;
        float dis = std::sqrt(x * x + y * y);
        int index = dis / delta_mR;
        if (index <= number) ring[index].emplace_back(glm::vec3{x, y, z});
    }

    std::vector<glm::vec3> pSet2;
    std::vector<glm::vec3> pSet3;

    for (auto& points : ring) {
        #if 0
        //根据与Y轴夹角大小排序，确保iter[0]位置
        std::sort(points.begin(),
                  points.end(),
                  [pO](const glm::vec3& p1, const glm::vec3& p2) {
                      glm::vec3 vec1 = p1 - pO;
                      glm::vec3 vec2 = p2 - pO;
                      glm::vec3 DY{0.f, 1.f, 0.f};
                      return std::acos(glm::dot(vec1, DY)) >
                             std::acos(glm::dot(vec2, DY));
                  });
        #endif
        glm::vec3 p2, p3;
        if (points.empty())
            continue;
        else if (points.size() == 1) {
            pSet2.emplace_back(points[0]);
            continue;
        } else if (points.size() == 2) {
            //环内有两个点
            p2 = glm::vec3(points[0].x, points[0].y, points[0].z);
            p3 = glm::vec3(points[1].x, points[1].y, points[1].z);
            pSet2.emplace_back(p2);
            pSet3.emplace_back(p3);
            continue;
        } else {
            glm::vec3 p1(points[0].x, points[0].y, points[0].z);
            float dis1 = 0.;
            float dis2 = 0.;
            for (auto& p : points) {
                float dis = std::sqrt((p.x - p1.x) * (p.x - p1.x) +
                                              (p.y - p1.y) * (p.y - p1.y));
                if (dis > dis1) {
                    dis1 = dis;
                    p2 = glm::vec3(p.x, p.y, p.z);  // p2为圆环中与p1距离最远点
                }
            }
            for (auto& p : points) {
                float dis = std::sqrt((p.x - p2.x) * (p.x - p2.x) +
                                              (p.y - p2.y) * (p.y - p2.y));
                if (dis > dis2) {
                    dis2 = dis;
                    p3 = glm::vec3(p.x, p.y, p.z);  // p3为圆环中与p2距离最远点
                }
            }
            pSet2.emplace_back(p2);
            pSet3.emplace_back(p3);
        }
    }

    if (pSet2.empty() || pSet3.empty()) throw std::runtime_error("pSet2 or pSet3 is empty");

    std::vector<glm::vec3> pSetTmp = pSet2;
    pSetTmp.insert(pSetTmp.end(), pSet3.begin(), pSet3.end());

    return pSetTmp;
}

void CalculateFlute::ExportFlutePoints(const std::vector<glm::vec3>& points)
{
    PROFILE_SCOPE("CalculateFlute::ExportFlutePoints");
    const std::string dirPath = "D:/Data/Study/vulkan/FirstApp/output_stuff/wear/Cpu_comparison";
    const std::string filePath = dirPath + "/flute_cpu.txt";

    std::filesystem::create_directories(dirPath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        ERROR("Failed to open output file: %s", filePath.c_str());
        return;
    }

    for (const auto& p : points) {
        file << p.x << ", " << p.y << '\n';
    }

    file.close();
    INFO("Exported %d flute points to %s", points.size(), filePath.c_str());
}

}  // namespace cpu