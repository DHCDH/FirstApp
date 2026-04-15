#include "GrindingWheel.h"

#include <gtc/quaternion.hpp>

#include "LveModel.h"
#include <gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/quaternion.hpp>
#include <gtx/vector_angle.hpp>

namespace entity
{
lve::LveObject GrindingWheel::CreateObject()
{
    // std::shared_ptr<lve::LveModel> lveModel =
    // lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    std::cout << "Read grinding wheel model"
              << "\n";
    p_model = lve::LveModel::CreateModelFromFile(GetRenderContext().device, m_filepath);
    auto grindingWheel = lve::LveObject::CreateObject();
    grindingWheel.model = p_model;
    
    grindingWheel.transform.translation = {0., -15., 0.};
    grindingWheel.transform.rotation = {-0.5255f, -0.6690f, -0.5255f};

    // 画线框
    grindingWheel.neededOutline = true;

    // grindingWheel.transform.translation = { 0.f, 0.f, 0.f };
    // grindingWheel.transform.rotation = {0.f, 0.f, 0.f};

    m_id = grindingWheel.getId();

    /*texture*/
    auto subCount = grindingWheel.model->GetSubmeshCount();
    GetRenderContext().submeshTextureSets[m_id].resize(subCount);
    GetRenderContext().submeshMatSets[m_id].resize(subCount);
    VkDescriptorSet dsAbrasive = GetRenderContext().textureManager.GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/TCom_OldAluminium_header.jpg",
        true);
    VkDescriptorSet dsMetal = GetRenderContext().textureManager.GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/Grinding_Wheel.jpg",
        true);
    GetRenderContext().submeshTextureSets[m_id][0] = dsAbrasive;
    // GetRenderContext().submeshTextureSets[m_id][1] = dsMetal;

    m_materialUBO.baseColorFactor = {1.f, 1.f, 0.f, 1.f};
    m_materialUBO.uvTilingOffset = {1, 1, 0, 0};
    m_materialUBO.pbrAoAlpha = {1.0f, 0.35f, 1.0f, 0.0f};  // 金属、粗糙低
    m_materialUBO.flags = {1u, 0u, 0u, 0u};

    // 为两个 submesh 生成“每帧一套”的 set=2
    CreateMaterialParamSetsForSubmesh(m_id, 0, m_materialUBO);
    // CreateMaterialParamSetsForSubmesh(m_id, 1, mtlMetal);

    m_helixMotion.M0 = m_modelMatrix;
    m_helixMotionInstanced.M0 = m_modelMatrix;
    return grindingWheel;
}

lve::TransformComponent GrindingWheel::Update(const float& dt)
{
    m_helixMotion.act += dt;
    lve::TransformComponent transform{};

    constexpr float pi = glm::pi<float>();
    float omega = 2 * pi * m_helixMotion.f / m_helixMotion.pitch;
    m_helixMotion.theta += omega * dt;
    m_helixMotion.y += m_helixMotion.f * dt;

    if (m_helixMotion.y > 100.f) {
        return transform;
    }

    transform.rotation = glm::vec3(0.f, m_helixMotion.theta, 0.f);
    transform.translation = glm::vec3(0.f, m_helixMotion.y, 0.f);
    transform.scale = glm::vec3(1.f);

    return transform;
}

lve::TransformComponent GrindingWheel::EvaluateAtTime(const float& t)
{
    constexpr float pi = glm::pi<float>();
    float pitch = m_helixMotionInstanced.pitch;
    float feedRate = m_helixMotionInstanced.f;
    float omega = 2 * pi * feedRate / pitch;

    m_helixMotionInstanced.theta = omega * t;
    m_helixMotionInstanced.y = feedRate * t;

    lve::TransformComponent transform{};
    transform.rotation = glm::vec3(0.f, m_helixMotionInstanced.theta, 0.f);
    transform.translation = glm::vec3(0.f, m_helixMotionInstanced.y, 0.f);
    transform.scale = glm::vec3(1.f);

    return transform;
}

void GrindingWheel::CalculateGrindingWheelInstances(
    std::vector<glm::mat4>& instances, const std::vector<ToolPath>& toolPaths) const
{
    lve::TransformComponent transform{};
    size_t totalSize = 0;
    for (const auto& path : toolPaths) totalSize += path.size;
    instances.reserve(totalSize);

    const glm::vec3 defaultPos{0, 0, 0};
    const glm::vec3 defaultNorm{1, 0, 0};

    int count = 0;
    for (const auto& path : toolPaths) {
        size_t size = path.size;
        // std::cout << "path[" << count << "]: size=" << size << "\n";
        for (size_t i = 0; i < size; i++) {
            const glm::vec3& pos = path.points[i];
            const glm::vec3& norm = path.normals[i];

            // std::cout << "toolpath[" << count << "][" << i
            //          << "]: pos(" << pos.x << ", " << pos.y << ", " << pos.z << "), "
            //          << "norm(" << norm.x << ", " << norm.y << ", " << norm.z << ")\n";

#if 0
            lve::TransformComponent transform{};
            transform.translation = pos;
            transform.scale = glm::vec3(1.f);

            // 计算旋转
            glm::quat rotationQuat = glm::rotation(defaultNorm, glm::normalize(norm));
            // 转为欧拉角
            transform.rotation = glm::eulerAngles(rotationQuat);

            lve::InstanceData instance{};
            instance.modelMatrix = transform.mat4();
#else
            // 1. 位移矩阵 (Translation)
            glm::mat4 translationMat = glm::translate(glm::mat4(1.0f), pos);

            // 2. 旋转矩阵 (Rotation) - 关键修改！
            // 直接从四元数转为矩阵，不要经过欧拉角
            glm::quat rotationQuat = glm::rotation(defaultNorm, glm::normalize(norm));
            glm::mat4 rotationMat = glm::toMat4(rotationQuat);

            // 3. 缩放矩阵 (Scale)
            glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(1.f));

            // 4. 组合最终矩阵 (M = T * R * S)
            // 注意乘法顺序：先缩放，再旋转，最后位移
            glm::mat4 instance = translationMat * rotationMat * scaleMat;
#endif

            instances.emplace_back(instance);

            if (i > 400) break;
        }
        // std::cout << "===count: " << count << "\n";

        count++;
    }

    return;
}
}  // namespace entity