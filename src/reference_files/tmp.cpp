/**
 * @brief calculate_matrix rewritten with glm::vec3 and glm::mat4.
 *
 * Mathematical result is identical to the original lucMatrix4x4 version.
 *
 * KEY STORAGE DIFFERENCE:
 *   lucMatrix4x4 is ROW-MAJOR:  Element[row][col] = flat[row*4 + col]
 *   glm::mat4      is COL-MAJOR: mat[col][row]       = flat[col*4 + row]
 *
 *   The flat-array representation of the SAME mathematical matrix is the
 *   TRANSPOSE between the two.  If you need to copy back to lucMatrix4x4
 *   after calling this function, you must copy as:
 *       for (int r = 0; r < 4; ++r)
 *           for (int c = 0; c < 4; ++c)
 *               lucMat->SetElement(r, c, glmMat[c][r]);
 *   or equivalently: glmMat = glm::transpose(glm::make_mat4(lucFlatPtr));
 */

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ===================================================================
// 1.  CalculatePoseMatrix  (was the lambda "calculate_matrix")
// ===================================================================

glm::mat4 CalculatePoseMatrix(const float point[3], const float norm[3])
{
    const glm::vec3 DY(0.0f, 0.0f, 1.0f);
    const glm::vec3 normal(norm[0], norm[1], norm[2]);

    glm::vec3 axis = glm::cross(DY, normal);
    float lambda = std::acos(glm::dot(DY, normal));

    if (glm::length(axis) > 1e-12f) {
        axis = glm::normalize(axis);
    }

    // Skew-symmetric cross-product matrix (K * v = axis × v), column-major GLM
    const glm::mat4 K(
        glm::vec4( 0.0f,      axis.z,  -axis.y,  0.0f),  // col0: axis × (1,0,0)
        glm::vec4(-axis.z,     0.0f,    axis.x,  0.0f),  // col1: axis × (0,1,0)
        glm::vec4( axis.y,   -axis.x,    0.0f,   0.0f),  // col2: axis × (0,0,1)
        glm::vec4( 0.0f,      0.0f,     0.0f,   1.0f)   // col3: homogeneous
    );

    // Rodrigues formula: R = I + sin(λ)·K + (1-cos(λ))·K²
    const glm::mat4 I(1.0f);
    const float    s  = std::sin(lambda);
    const float    c1 = 1.0f - std::cos(lambda);

    glm::mat4 R = I + s * K + c1 * (K * K);

    // Set translation column
    R[3] = glm::vec4(point[0], point[1], point[2], 1.0f);

    return R;
}


// ===================================================================
// 2.  SolveNewton2D  (was lucChipFluteModel::SolveNewton2D)
// ===================================================================
//
// Access mapping (original row-major A[row][col] → glm column-major mat[col][row]):
//   A[0][*] → mat[*][0]    A[1][*] → mat[*][1]    A[2][*] → mat[*][2]
//
glm::vec3 SolveNewton2D(float m1, float t, float z, float fg,
                        const glm::mat4& mat,
                        float lead, float rBlade, bool isStraight)
{
    // ---- reachable rows from the upper-left 3×4 block -----------------
    // A[0][*] = row 0:  (mat[0][0], mat[1][0], mat[2][0], mat[3][0])
    // A[1][*] = row 1:  (mat[0][1], mat[1][1], mat[2][1], mat[3][1])
    // A[2][*] = row 2:  (mat[0][2], mat[1][2], mat[2][2], mat[3][2])

    const float A00 = mat[0][0], A01 = mat[1][0], A02 = mat[2][0], A03 = mat[3][0];
    const float A10 = mat[0][1], A11 = mat[1][1], A12 = mat[2][1], A13 = mat[3][1];
    const float A20 = mat[0][2], A21 = mat[1][2], A22 = mat[2][2], A23 = mat[3][2];

    float lambda = 0.0f;

    // ---- coordinate expressions --------------------------------------
    auto mxQt = [&](float m2) -> float {
        if (isStraight) {
            return (A03 + A02 * m1 + A00 * fg * std::cos(m2) +
                    A01 * fg * std::sin(m2));
            // The second term multiplied by 0 in the original is omitted.
        } else {
            return std::cos(t - lambda) * (A03 + A02 * m1 + A00 * fg * std::cos(m2) +
                                           A01 * fg * std::sin(m2)) -
                   std::sin(t - lambda) * (A13 + A12 * m1 + A10 * fg * std::cos(m2) +
                                           A11 * fg * std::sin(m2));
        }
    };

    auto myQt = [&](float m2) -> float {
        if (isStraight) {
            return (A13 + A12 * m1 + A10 * fg * std::cos(m2) +
                    A11 * fg * std::sin(m2));
        } else {
            return std::cos(t - lambda) * (A13 + A12 * m1 + A10 * fg * std::cos(m2) +
                                           A11 * fg * std::sin(m2)) +
                   std::sin(t - lambda) * (A03 + A02 * m1 + A00 * fg * std::cos(m2) +
                                           A01 * fg * std::sin(m2));
        }
    };

    auto F = [&](float m2) -> float {
        if (isStraight) {
            return A23 + A22 * m1 + A20 * fg * std::cos(m2) +
                   A21 * fg * std::sin(m2) + t * 0.5f - z;
        } else {
            return A23 + A22 * m1 + A20 * fg * std::cos(m2) +
                   A21 * fg * std::sin(m2) +
                   (lead * (t - lambda)) / (2.0f * glm::pi<float>()) - z;
        }
    };

    auto dF_dm2 = [&](float m2) -> float {
        const float h = 1e-4f;
        return (F(m2 + h) - F(m2 - h)) / (2.0f * h);
    };

    // ---- Newton-Raphson over initial guesses -------------------------
    const float epsilon   = 1e-7f;
    const float M2        = 2.0f * glm::pi<float>();
    const float deltaM2   = M2 / 10.0f;
    const float m2Seeds[] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };

    for (float seed : m2Seeds) {
        for (float m2 = seed; m2 <= M2; m2 += deltaM2) {
            int iter = 0;
            float m2_cur = m2;

            while (iter < 100) {
                float fVal = F(m2_cur);
                float df   = dF_dm2(m2_cur);

                if (std::abs(df) < 1e-10f) break;

                if (std::abs(fVal) < epsilon) {
                    float x = mxQt(m2_cur);
                    float y = myQt(m2_cur);
                    if (x * x + y * y <= rBlade * rBlade) {
                        return glm::vec3(x, y, z);
                    }
                    break;
                }

                m2_cur -= fVal / df;
                ++iter;
            }
        }
    }

    return glm::vec3(-1.0f, -1.0f, -1.0f);  // no solution found
}


// ===================================================================
// 3.  CalculateChipFlutePointSet  (was lucChipFluteModel::CalculateChipFlutePointSet)
// ===================================================================
//
// Returns a vector of 3D points that form the chip-flute profile.
// Parameters extracted from the original member-function context:
//   lead           – m_lead           (helix lead)
//   rBlade         – m_rBlade         (blade radius)
//   isStraight     – m_isStraight     (straight-flute flag)
//   gRanges        – m_gRanges        (grinding-wheel segment m1-ranges)
//   gEquations     – m_gEquations     (grinding-wheel profile equations f(m1))
//   wheelDiameter  – m_gWheel->GetDiameter()  (only used when isStraight)
//   gZ             – m_gZ             (fallback Z, only used when isStraight)

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

std::vector<glm::vec3> CalculateChipFlutePointSet(
    const glm::mat4&                          matrix,
    float                                     lead,
    float                                     rBlade,
    bool                                      isStraight,
    const std::vector<std::pair<float, float>>& gRanges,
    const std::vector<std::function<float(float)>>& gEquations,
    float wheelDiameter = 0.0f,
    float gZ            = 0.0f)
{
    const float T = 0.5f * glm::pi<float>();

    // ---- threaded sampling kernel ---------------------------------------
    std::mutex mtx;

    auto calculate_points_t =
        [&](std::vector<glm::vec3>& coordVec, float z, int k, float deltaT) {
            float t = static_cast<float>(k) * deltaT;
            std::vector<glm::vec3> local;

            for (size_t j = 0; j < gEquations.size(); ++j) {
                float M1      = gRanges[j].second;
                float deltaM1 = 0.01f;

                for (float m1 = gRanges[j].first; m1 < M1 + deltaM1; m1 += deltaM1) {
                    if (m1 > M1) m1 = M1;

                    float fg = gEquations[j](m1);
                    glm::vec3 pt = SolveNewton2D(m1, t, z, fg, matrix,
                                                 lead, rBlade, isStraight);

                    if (pt.z > -0.5f) {   // sentinel is (-1,-1,-1), valid z ≥ 0
                        local.push_back(pt);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mtx);
                coordVec.insert(coordVec.end(), local.begin(), local.end());
            }
        };

    // ---- launch threads over angular samples ---------------------------
    const float z = 0.0f;
    std::vector<glm::vec3> coordVec;

    int   nT     = static_cast<int>(std::abs(T / 0.001f));
    if (rBlade > 10.0f) {
        nT = static_cast<int>(std::abs(T / 0.002f));
    }
    float deltaT = T / static_cast<float>(nT);

    if (isStraight) {
        nT     = static_cast<int>(wheelDiameter);
        deltaT = 1.0f;
    }

    {
        std::counting_semaphore<16> sem(16);
        std::vector<std::thread>    threads;

        for (int k = -nT; k <= nT; ++k) {
            if (sem.try_acquire_for(std::chrono::seconds(5))) {
                threads.emplace_back([&, z, k, deltaT]() {
                    try {
                        calculate_points_t(coordVec, z, k, deltaT);
                    } catch (const std::exception& e) {
                        std::cerr << "Exception: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "Unknown exception" << std::endl;
                    }
                    sem.release();
                });
            }
        }

        for (auto& th : threads) th.join();
    }

    return coordVec;
}


// ===================================================================
// Conversion note (lucMatrix4x4 ↔ glm::mat4)
// ===================================================================
//
// When the caller receives a glm::mat4 result (e.g. from CalculatePoseMatrix)
// and needs to store it back into lucMatrix4x4:
//
//   lucMatrix4x4* out = lucMatrix4x4::New();
//   for (int r = 0; r < 4; ++r)
//       for (int c = 0; c < 4; ++c)
//           out->SetElement(r, c, static_cast<double>(mat[c][r]));
//
// Because:  lucMatrix4x4 (row-major)  → Element[r][c] = math M[r][c]
//           glm::mat4    (col-major)  →     mat[c][r] = math M[r][c]
