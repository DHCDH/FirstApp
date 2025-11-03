#include "FirstApp.h"

#include <stdexcept>
#include <array>
#include <iostream>
#include <chrono>

namespace lve {

static constexpr float ORBIT_SENS = 0.005f; // 每像素旋转弧度
static constexpr float PAN_SENS = 0.002f;   // 每像素平移比例
static constexpr float DOLLY_RATE = 0.12f;    // 滚轮step的缩放比率

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
    m_renderSystem = std::make_unique<RenderSystem>(*m_lveDevice, m_lveRenderer->GetSwapChainRenderPass());
}

void FirstApp::runFrame()
{
    if (m_lveWindow->WasWindowResized()) {
        m_lveWindow->ResetWindowResizedFlag();
        m_lveRenderer->RecreateSwapChain();
    }

    VkCommandBuffer commandBuffer = m_lveRenderer->BeginFrame();
    if (commandBuffer == nullptr) {
        /*此处不做任何阻塞，等待下一次QTimer*/
        return;
    }

    /*进入本帧的主RenderPass*/
    m_lveRenderer->BeginSwapChainRenderPass(commandBuffer);

    /*设置相机的视图与投影*/
    float aspect = m_lveRenderer->GetAspectRatio(); // 宽高比
    m_lveCamera->SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 10.f);
    // m_lveCamera->SetViewTarget(glm::vec3(0.f, 0.f, -2.f), glm::vec3(0.f, 0.f, 1.f));
    UpdateCameraFromOrbit();

    /*绘制*/
    m_renderSystem->RenderGameObjects(commandBuffer, m_objects, *m_lveCamera);
    m_renderSystem->RenderAxis(commandBuffer, *m_lveCamera, m_lveRenderer->GetSwapChainExtent());

    /*结束本帧RenderPass并提交*/
    m_lveRenderer->EndSwapChainRenderPass(commandBuffer);
    m_lveRenderer->EndFrame();
}

void FirstApp::LoadObjects() {
    std::shared_ptr<LveModel> lveModel = LveModel::CreateModelFromFile(*m_lveDevice, "res/models/flat_vase.obj");
    auto flatVase = LveObject::CreateObject();
    flatVase.model = lveModel;
    flatVase.transform.translation = { -.5f, .5f, 2.5f };
    // gameObj.transform.scale = glm::vec3(3.f);
    flatVase.transform.scale = { 3.f, 1.5f, 3.f };
    m_objects.push_back(std::move(flatVase));

    lveModel = LveModel::CreateModelFromFile(*m_lveDevice, "res/models/smooth_vase.obj");
    auto smoothVase = LveObject::CreateObject();
    smoothVase.model = lveModel;
    smoothVase.transform.translation = { .5f, .5f, 2.5f };
    // gameObj.transform.scale = glm::vec3(3.f);
    smoothVase.transform.scale = { 3.f, 1.5f, 3.f };
    m_objects.push_back(std::move(smoothVase));
}

void FirstApp::UpdateCameraFromOrbit()
{
    float cy = std::cos(m_orbit.yaw);
    float sy = std::sin(m_orbit.yaw);
    float cp = std::cos(m_orbit.pitch);
    float sp = std::sin(m_orbit.pitch);

    /*相机看向的方向*/
    glm::vec3 forward = glm::normalize(glm::vec3(cp * sy, sp, -cp * cy));

    glm::vec3 position = m_orbit.target - forward * m_orbit.distance;
    m_lveCamera->SetViewTarget(position, m_orbit.target);
}

void FirstApp::Orbit(float dxPixels, float dyPixels)
{
    m_orbit.yaw += dxPixels * ORBIT_SENS;
    m_orbit.pitch -= dyPixels * ORBIT_SENS;

    const float kPole = 1.2f;
    m_orbit.pitch = glm::clamp(m_orbit.pitch, -kPole, kPole);

    constexpr float PI = glm::pi<float>();
    float TAU = 2.f * PI;
    if (m_orbit.yaw > PI) m_orbit.yaw -= TAU;
    if (m_orbit.yaw < -PI) m_orbit.yaw += TAU;
}

void FirstApp::Pan(float dxPixels, float dyPixels)
{
    /*基于当前 forward 计算 right / up（世界上方向取(0, 1, 0)）*/
    float cy = std::cos(m_orbit.yaw), sy = std::sin(m_orbit.yaw);
    float cp = std::cos(m_orbit.pitch), sp = std::sin(m_orbit.pitch);

    glm::vec3 forward = glm::normalize(glm::vec3(cp * sy, sp, -cp * cy));
    glm::vec3 worldUp{ 0.f, 1.f, 0.f };
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    float scale = std::max(0.1f, m_orbit.distance) * PAN_SENS;
    // 屏幕坐标通常 x 右正、y 下正；如果想拖动方向完全模仿 VTK，可按需要取反
    m_orbit.target -= right * (dxPixels * scale);
    m_orbit.target += up * (dyPixels * scale);
}

void FirstApp::Dolly(float steps)
{
    float scale = std::pow(1.f - DOLLY_RATE, steps);
    m_orbit.distance = glm::clamp(m_orbit.distance * scale, 0.05f, 100.f);
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