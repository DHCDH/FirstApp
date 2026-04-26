#include "GrindingWheelWearCalculator.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

#include "Logger.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <gtc/quaternion.hpp>
#include <gtx/quaternion.hpp>

static const std::string FLUTE_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/flute_points_wear_extract.txt";
static const std::string OUTPUT_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/points_wear_fitted.txt";
static const std::string OUTPUT_HELIX_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/points_wear_helix.txt";
static const std::string OUTPUT_CONTACT_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/points_wear_contact.txt";
static const std::string OUTPUT_WHEEL_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/wheel_profile.txt";
static const std::string OUTPUT_FLUTE_POINTS_FILEPATH =
    "D:/Data/Study/vulkan/FirstApp/output_stuff/flute_points_wear_output.txt";
static const float PI = glm::pi<float>();
static const float MAX = std::numeric_limits<float>::max();

namespace wear
{
GrindingWheelWearCalculator::GrindingWheelWearCalculator(const CutterParameters& cp,
                                                         const WearParameters& wp)
    : m_cp(cp), m_wp(wp)
{
    INFO("Construct GrindingWheelWearCalculator");
    ParseFlutePointSet(FLUTE_FILEPATH);
    FitFluteProfile();
    ExportFittedCurveToTXT(OUTPUT_FILEPATH);
    CalaulateWheelWearProfile();
}

void GrindingWheelWearCalculator::CalaulateWheelWearProfile()
{
    uint32_t nA = 200;
    uint32_t nB = 200;

    // phi的搜索范围，搜索范围在 0-2π内
    double phiStart = 0;
    double phiEnd = PI;
    double dPhi = (phiEnd - phiStart) / nB;

    std::vector<glm::vec3> contactLine;  // 接触线点集

    std::vector<glm::vec3> flutePoints(nA);
    for (uint32_t i = 0; i < nA; i++) {
        float t = (float)i / (float)(nA - 1) * m_maxT;
        float tNxt = (float)(i + 1) / (float)(nA - 1) * m_maxT;

        // 获取端面容屑槽上一点
        double xm = GetProfileX(t);
        double ym = GetProfileY(t);
        double dxm = GetProfileDx(t);
        double dym = GetProfileDy(t);

        double xmNxt = GetProfileX(tNxt);
        double ymNxt = GetProfileY(tNxt);
        double dxmNxt = GetProfileDx(tNxt);
        double dymNxt = GetProfileDy(tNxt);

        flutePoints.emplace_back(xm, ym, 0.0);

        // --- 在螺旋线上取nB个离散点 ---
        std::vector<glm::vec4> Bptset;  // (x, y, z, phi)
        for (uint32_t j = 0; j < nB; j++) {
            float phi = j * dPhi;
            float x = xm * std::cos(phi) - ym * std::sin(phi);
            float y = xm * std::sin(phi) + ym * std::cos(phi);
            float z = m_cp.lead / (2.f * PI) * phi;
            Bptset.emplace_back(x, y, z, phi);
            m_helixPoints.emplace_back(x, y, z);
        }

        DEBUG("diff y: %f, dy: %f", ymNxt - ym, dym / dxm);

        float minDist = MAX;
        glm::vec3 pBest{Bptset[0].x, Bptset[0].y, Bptset[0].z};
        std::vector<glm::vec3> nQ1(nB);
        for (uint32_t j = 0; j < Bptset.size(); j++) {
            glm::vec4& pB = Bptset[j];

            glm::vec3 p{pB.x, pB.y, pB.z};

            // --- 计算容屑槽表面在点集Bptset中每点处的法向量nQ1 ---
            glm::vec3 tauQ1{};
            tauQ1.x = -xm * std::sin(pB.w) - ym * std::cos(pB.w);
            tauQ1.y = -xm * std::sin(pB.w) + xm * std::cos(pB.w);
            tauQ1.z = m_cp.lead / (2.f * PI);

            glm::vec3 tauQ1Pi{};
            tauQ1Pi.x = xmNxt - xm;
            tauQ1Pi.y = ymNxt - ym;
            tauQ1Pi.z = 0.f;

            glm::vec3 nQ = glm::cross(tauQ1, tauQ1Pi);
            nQ1.emplace_back(nQ);

            // --- 计算法向与砂轮轴线距离 ---
            glm::vec3 gwNormal = glm::normalize(m_wp.normal);
            glm::vec3 crossProd = glm::cross(nQ, gwNormal);
            float cpLength = glm::length(crossProd);


            float distance = 0.f;
            if (cpLength < 1e-8) {
                distance = glm::length(glm::cross(p - m_wp.position, m_wp.normal));
            } else {
                // 异面直线最短距离公式
                distance = std::abs(glm::dot(p - m_wp.position, crossProd)) / cpLength;
            }

            if (distance < minDist) {
                minDist = distance;
                pBest = p;
            }

        }
        contactLine.push_back(pBest);
    }
    
    DEBUG("Flute points size: %d", flutePoints.size());

    ExportVec3ToTXT(flutePoints, OUTPUT_FLUTE_POINTS_FILEPATH);
    ExportVec3ToTXT(m_helixPoints, OUTPUT_HELIX_FILEPATH);
    ExportVec3ToTXT(contactLine, OUTPUT_CONTACT_FILEPATH);

    return;
}

void GrindingWheelWearCalculator::FitFluteProfile()
{
    if (m_flutePointSet.empty()) {
        ERROR("No points to fit.");
        return;
    }

    // --- 数据清洗，合并相同的X坐标 ---
    std::vector<double> T;  // 累积弧长参数
    std::vector<double> X;
    std::vector<double> Y;

    // 弦长参数化 (Chord-Length Parameterization)
    double currentT = 0.0;
    T.push_back(currentT);
    X.push_back(m_flutePointSet[0].x);
    Y.push_back(m_flutePointSet[0].y);

    for (size_t i = 1; i < m_flutePointSet.size(); ++i) {
        // 计算相邻两点之间的欧氏距离
        float dist = glm::distance(m_flutePointSet[i], m_flutePointSet[i - 1]);

        // 过滤掉距离过近的重合点（防止 T 不严格递增导致 spline 报错）
        if (dist < 1e-5) continue;

        if (dist > 1.f) {
            DEBUG(
                "Abnormal jump in point set (distance: %.2f). Subsequent data "
                "auto-truncated.",
                dist);
            break;
        }

        currentT += dist;
        T.push_back(currentT);
        X.push_back(m_flutePointSet[i].x);
        Y.push_back(m_flutePointSet[i].y);
    }

    m_maxT = currentT;  // 保存最大边界

    // 分别拟合两条样条曲线
    try {
        m_splineX.set_points(T, X);
        m_splineY.set_points(T, Y);
        m_isFitted = true;
        INFO("Parametric Spline fitting completed.");
    } catch (const std::exception& e) {
        ERROR("Spline fitting failed: %s", e.what());
    }
}

float GrindingWheelWearCalculator::GetProfileX(float t) const
{
    if (!m_isFitted) return 0.0f;
    return static_cast<float>(m_splineX(static_cast<double>(t)));
}

float GrindingWheelWearCalculator::GetProfileY(float t) const
{
    if (!m_isFitted) return 0.0f;
    return static_cast<float>(m_splineY(static_cast<double>(t)));
}

float GrindingWheelWearCalculator::GetProfileDx(float t) const
{
    if (!m_isFitted) return 0.0f;
    return static_cast<float>(m_splineX.deriv(1, static_cast<double>(t)));
}

float GrindingWheelWearCalculator::GetProfileDy(float t) const
{
    if (!m_isFitted) return 0.0f;
    return static_cast<float>(m_splineY.deriv(1, static_cast<double>(t)));
}

bool GrindingWheelWearCalculator::ParseFlutePointSet(const std::string& filepath)
{
    m_flutePointSet.clear();

    std::ifstream inFile(filepath, std::ios::in);
    if (!inFile.is_open()) {
        ERROR("Failed to open file: %s", filepath.c_str());
        return false;
    }

    std::string line;

    while (std::getline(inFile, line)) {
        // 跳过空行
        if (line.empty()) continue;

        try {
            // 1. 提取括号内的坐标内容：去除(和)
            size_t leftBracket = line.find('(');
            size_t rightBracket = line.find(')');
            if (leftBracket == std::string::npos || rightBracket == std::string::npos ||
                leftBracket >= rightBracket)
                continue;

            std::string coordContent =
                line.substr(leftBracket + 1, rightBracket - leftBracket - 1);

            // 2. 按逗号分割x、y坐标
            size_t commaPos = coordContent.find(',');
            if (commaPos == std::string::npos) continue;

            // 3. 字符串转浮点型坐标
            float x = std::stof(coordContent.substr(0, commaPos));
            float y = std::stof(coordContent.substr(commaPos + 1));

            // 刀轨文件与论文坐标系不一致，需要转换
            float xm = y;
            float ym = x;

            // 4. 存入glm::vec2并添加到点集
            m_flutePointSet.emplace_back(xm, ym, 0);
        } catch (const std::exception& e) {
            // 捕获格式错误，跳过异常行
            DEBUG("Parse failed -> %s.", line);
            continue;
        }
    }

    inFile.close();

    INFO("Parse flute points file succeed: total points size: %d",
         m_flutePointSet.size());
    return true;
}

void GrindingWheelWearCalculator::ExportFittedCurveToTXT(
    const std::string& filename) const
{
    if (!m_isFitted || m_flutePointSet.empty()) return;

    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        ERROR("Failed to create CSV file.");
        return;
    }

    // 密集采样：比如在区间内均匀取 1000 个点
    int numSamples = 1000;
    float step = (m_maxT - 0) / (numSamples - 1);

    for (int i = 0; i < numSamples; ++i) {
        float x = GetProfileX(i * step);
        float y = GetProfileY(i * step);  // 相当于公式 21 计算结果
        float z = 0.f;

        if (i == 0) {
            DEBUG("FlutPosition[0]: (%f, %f, %f)", x, y, z);
        }

        outFile << "(" << x << "," << y << "," << z << ")"
                << "\n";
    }

    outFile.close();
    INFO("Fitted curve exported to %s", filename.c_str());
}

void GrindingWheelWearCalculator::ExportVec3ToTXT(const std::vector<glm::vec3>& vec,
                                                  const std::string& filename) const
{
    DEBUG("Ready to export vec3 to file: %s", filename.c_str());

    if (vec.empty()) {
        return;
    }

    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        ERROR("Failed to create HelixPoints file.");
        return;
    }

    for (const auto& p : vec) {
        outFile << "(" << p.x << "," << p.y << "," << p.z << ")"
                << "\n";
    }

    outFile.close();
    INFO("Exported to %s", filename.c_str());
}

void GrindingWheelWearCalculator::ExportWheelProfile(
    const std::vector<glm::vec2>& profile, const std::string& filename)
{
    std::ofstream outFile(filename);
    for (const auto& p : profile) {
        // 输出格式为: zg, rho
        outFile << "(" << p.x << "," << p.y << ")\n";
    }
    INFO("Wheel profile exported to %s", filename.c_str());
    outFile.close();
}

GrindingWheelWearCalculator::~GrindingWheelWearCalculator()
{
}

}  // namespace wear