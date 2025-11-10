#include "FirstApp.h"

#include "lve/LveBuffer.h"

#include <stdexcept>
#include <array>
#include <iostream>
#include <numeric>

namespace lve {

struct OrbitConfig {
    float rotateSpeedPerPixel = 0.0020f;   // 每像素约 0.11°
    float panBasePerPixel = 0.0015f;   // 每像素平移尺度 = distance * 该系数
    float dollySpeed = 0.22f;     // 指数缩放强度（滚轮步长）
    float minDistance = 0.05f;     // 缩放最近距离
    float maxDistance = 500.0f;    // 缩放最远距离
    float minPitch = -1.55334306f; // -89° (弧度)
    float maxPitch = 1.55334306f; //  89° (弧度)
    float fovY = 0.87266463f; //  50° (弧度) 供平移尺度估算
}orbitCfg;

FirstApp::FirstApp(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name)
{
    SetLveComponants(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    LoadObjects();
}

void FirstApp::SetLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name)
{
    m_lveWindow = std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    m_lveDevice = std::make_unique<LveDevice>(*m_lveWindow);
    m_lveRenderer = std::make_unique<LveRenderer>(*m_lveWindow, *m_lveDevice);
    m_lveCamera = std::make_unique<LveCamera>();

    /*ubo*/
    m_globalPool = LveDescriptorPool::Builder(*m_lveDevice)
        .SetMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .Build();

    m_uboBuffers.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT); //2
    for (int i = 0; i < m_uboBuffers.size(); i++) {
        m_uboBuffers[i] = std::make_unique<LveBuffer>(
            *m_lveDevice,
            sizeof(GlobalUbo),
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        m_uboBuffers[i]->Map();
    }

    m_globalSetLayout = LveDescriptorSetLayout::Builder(*m_lveDevice)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
        .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build();
    m_globalDescriptorSets.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < m_globalDescriptorSets.size(); i++) {
        auto bufferInfo = m_uboBuffers[i]->DescriptorInfo(); // binding 0

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler = sampler;
        imgInfo.imageView = imageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        LveDescriptorWriter(*m_globalSetLayout, *m_globalPool)
            .WriteBuffer(0, &bufferInfo)
            .WriteImage(1, &imgInfo)
            .Build(m_globalDescriptorSets[i]);
    }

    m_renderSystem = std::make_unique<RenderSystem>(*m_lveDevice, 
        m_lveRenderer->GetSwapChainRenderPass(), m_globalSetLayout->GetDescriptorSetLayout());
    m_pointLightSystem = std::make_unique<PointLightSystem>(*m_lveDevice,
        m_lveRenderer->GetSwapChainRenderPass(), m_globalSetLayout->GetDescriptorSetLayout());

    m_lastTick = std::chrono::high_resolution_clock::now();
}

void FirstApp::runFrame()
{
    if (m_lveWindow->WasWindowResized()) {
        m_lveWindow->ResetWindowResizedFlag();
        m_lveRenderer->RecreateSwapChain();
    }

    auto now = std::chrono::high_resolution_clock::now();
    m_frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(now - m_lastTick).count();
    m_lastTick = now;

    VkCommandBuffer commandBuffer = m_lveRenderer->BeginFrame();
    if (commandBuffer == nullptr) {
        /*此处不做任何阻塞，等待下一次QTimer*/
        return;
    }

    /*设置相机的视图与投影*/
    float aspect = m_lveRenderer->GetAspectRatio(); // 宽高比
    m_lveCamera->SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);
    // m_lveCamera->SetViewTarget(glm::vec3(0.f, 0.f, -2.f), glm::vec3(0.f, 0.f, 1.f));
    UpdateCameraFromOrbit();

    int frameIndex = m_lveRenderer->GetFrameIndex();
    FrameInfo frameInfo{
        frameIndex,
        m_frameTimeSec,
        commandBuffer,
        *m_lveCamera,
        m_globalDescriptorSets[frameIndex],
        m_objects
    };

    /*更新跟随镜头点光源*/
    glm::vec3 camPos = m_lveCamera->GetPosition();
    if (auto it = m_objects.find(m_headlightId); it != m_objects.end()) {
        it->second.transform.translation = camPos;

        float d = m_orbit.distance;
        it->second.pointLight->lightIntensity = .3f * d * d;
    }

    /*将PV矩阵写入UBO*/
    GlobalUbo ubo{};
    ubo.projection = m_lveCamera->GetProjection();
    ubo.view = m_lveCamera->GetView();
    ubo.inverseView = m_lveCamera->GetInverseView();
    ubo.ambientLightColor.w = 0.08f;
    m_pointLightSystem->Update(frameInfo, ubo);
    m_uboBuffers[frameIndex]->WriteToBuffer(&ubo);

    /*进入本帧的主RenderPass*/
    m_lveRenderer->BeginSwapChainRenderPass(commandBuffer);

    /*绘制*/
    m_renderSystem->RenderObjects(frameInfo);
    m_pointLightSystem->Render(frameInfo);

    /*结束本帧RenderPass并提交*/
    m_lveRenderer->EndSwapChainRenderPass(commandBuffer);
    m_lveRenderer->EndFrame();
}

void FirstApp::LoadObjects() 
{
    std::shared_ptr<LveModel> lveModel = LveModel::CreateModelFromFile(*m_lveDevice, "D:/Data/Study/vulkan/FirstApp/res/models/cutter.obj");
    auto quad = LveObject::CreateObject();
    quad.model = lveModel;
    quad.transform.translation = { 0.f, .5f, 0.f };
    quad.transform.scale = { 1.f, 1.f, 1.f };
    m_objects.emplace(quad.getId(), std::move(quad));

    /*创建跟随相机的点光源*/
    auto head = LveObject::MakePointLight(3., 0.25, { 1.f, .86f, .55f });
    m_headlightId = head.getId();
    m_objects.emplace(head.getId(), std::move(head));

    std::vector<glm::vec3> lightColors{
        {1.f, .1f, .1f},
        {.1f, .1f, 1.f},
        {.1f, 1.f, .1f},
        {1.f, 1.f, .1f},
        {.1f, 1.f, 1.f},
        {1.f, 1.f, 1.f},
    };

    for (int i = 0; i < lightColors.size(); i++) {
        auto pointLight = LveObject::MakePointLight(10.f);
        pointLight.color = lightColors[i];
        auto rotateLight = glm::rotate(glm::mat4(1.f), (i * glm::two_pi<float>()) / lightColors.size(), { 0.f, -1.f, 0.f });
        pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
        // m_objects.emplace(pointLight.getId(), std::move(pointLight));
    }
}

void FirstApp::UpdateCameraFromOrbit()
{
    const float cy = std::cos(m_orbit.yaw),  sy = std::sin(m_orbit.yaw);
    const float cp = std::cos(m_orbit.pitch), sp = std::sin(m_orbit.pitch);
    glm::vec3 offset(
        m_orbit.distance *  cp * sy,
        m_orbit.distance *  sp,
        m_orbit.distance *  cp * cy
    );
    glm::vec3 camPos = m_orbit.target - offset;
    // 依你项目的 API 设置相机，这里用 setViewTarget 示例
    m_lveCamera->SetViewTarget(camPos, m_orbit.target);
    // 若有随相机移动的点光源，可在此同步（可选）
}

void FirstApp::Orbit(float dxPixels, float dyPixels)
{
    m_orbit.yaw   -= dxPixels * orbitCfg.rotateSpeedPerPixel;
    m_orbit.pitch -= dyPixels * orbitCfg.rotateSpeedPerPixel;
    // 俯仰夹取
    if (m_orbit.pitch > orbitCfg.maxPitch) m_orbit.pitch = orbitCfg.maxPitch;
    if (m_orbit.pitch < orbitCfg.minPitch) m_orbit.pitch = orbitCfg.minPitch;
    // 将 yaw 归一到 [-pi, pi]，避免数值漂移
    if (m_orbit.yaw >  glm::pi<float>())  m_orbit.yaw -= glm::two_pi<float>();
    if (m_orbit.yaw < -glm::pi<float>())  m_orbit.yaw += glm::two_pi<float>();
    UpdateCameraFromOrbit();
}

void FirstApp::Pan(float dxPixels, float dyPixels)
{
    const float panScale = m_orbit.distance * orbitCfg.panBasePerPixel;
    const float cy = std::cos(m_orbit.yaw),  sy = std::sin(m_orbit.yaw);
    const float cp = std::cos(m_orbit.pitch), sp = std::sin(m_orbit.pitch);
    // 朝向（从相机指向目标）
    glm::vec3 forward = glm::normalize(glm::vec3(cp*sy, sp, cp*cy));
    glm::vec3 worldUp = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 right   = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up      = glm::normalize(glm::cross(right,   forward));
    // 屏幕像素位移 -> 世界位移（注意屏幕 y 向下为正）
    m_orbit.target += (-dxPixels * panScale) * right;
    m_orbit.target += ( dyPixels * panScale) * up;
    UpdateCameraFromOrbit();
}

void FirstApp::Dolly(float steps)
{
    const float k = std::exp(-steps * orbitCfg.dollySpeed); // steps>0 拉近
    m_orbit.distance = glm::clamp(m_orbit.distance * k, orbitCfg.minDistance, orbitCfg.maxDistance);
    UpdateCameraFromOrbit();
}

void FirstApp::ResetView()
{
    m_orbit.target = { 0.f, 0.f, 2.5f };
    m_orbit.distance = 5.0f;
    m_orbit.yaw = glm::pi<float>();
    m_orbit.pitch = 0.f;
}

void FirstApp::WaitIdle()
{
    if (m_lveDevice) {
        vkDeviceWaitIdle(m_lveDevice->device());
    }
}

FirstApp::~FirstApp()
{
    if (m_lveDevice) {
        vkDeviceWaitIdle(m_lveDevice->device());
    }
}

}