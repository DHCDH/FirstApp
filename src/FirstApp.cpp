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
    InitLveComponants(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    LoadObjects();
}

void FirstApp::InitLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name)
{
    m_lveWindow = std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    m_lveDevice = std::make_unique<LveDevice>(*m_lveWindow);
    m_lveRenderer = std::make_unique<LveRenderer>(*m_lveWindow, *m_lveDevice);
    m_lveCamera = std::make_unique<LveCamera>();
    m_texture = std::make_unique<LveTexture>(
        *m_lveDevice, 
        "D:/Data/Study/vulkan/FirstApp/res/textures/TCom_BrushedStainlessSteel_header.jpg", 
        true);

    /*global ubo*/
    m_globalPool = LveDescriptorPool::Builder(*m_lveDevice)
        .SetMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, LveSwapChain::MAX_FRAMES_IN_FLIGHT)
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
        .Build();
    m_globalDescriptorSets.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < m_globalDescriptorSets.size(); i++) {
        auto bufferInfo = m_uboBuffers[i]->DescriptorInfo(); // binding 0

        LveDescriptorWriter(*m_globalSetLayout, *m_globalPool)
            .WriteBuffer(0, &bufferInfo)
            .Build(m_globalDescriptorSets[i]);
    }

    /*texture, set = 1*/
    m_materialPool = LveDescriptorPool::Builder(*m_lveDevice)
        .SetMaxSets(128)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128)
        .Build();
    m_materialSetLayout = LveDescriptorSetLayout::Builder(*m_lveDevice)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
        .Build();

    /*创建采样器Sampler*/
    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR;   // 放大采用线性过滤
    sci.minFilter = VK_FILTER_LINEAR;   // 缩小采用线性过滤
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;    // 重复寻址，适合可平铺贴图
    sci.anisotropyEnable = VK_TRUE; // 开启各向异性过滤
    sci.maxAnisotropy = 16.0f;
    sci.minLod = 0.0f; sci.maxLod = 100.0f; // 若你生成了 mipmap，这样才能访问所有 mip
    if (vkCreateSampler(m_lveDevice->device(), &sci, nullptr, &m_sharedSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sampler!");
    }

    m_textureManager = std::make_unique<LveTextureManager>(*m_lveDevice, m_sharedSampler, *m_materialSetLayout, *m_materialPool);

    // 占位 set=1：随便复用一张已有纹理（或你做一张 1x1 白图）
    m_defaultTextureSet = m_textureManager->GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/white_1x1.png", true);

    // set = 2，材质参数UBO
    m_materialParamPool = lve::LveDescriptorPool::Builder(*m_lveDevice)
        .SetMaxSets(128 * lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128 * lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT)
        .Build();

    m_materialParamSetLayout = lve::LveDescriptorSetLayout::Builder(*m_lveDevice)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT) // 先指向 FS，可先不使用
        .Build();

    MaterialUBO def{};
    def.baseColorFactor = { 1,1,1,1 };
    def.uvTilingOffset = { 1,1,0,0 };
    def.pbrAoAlpha = { 0.0f, 0.5f, 1.0f, 0.0f };
    def.flags = { 0u,0u,0u,0u };

    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        auto buf = std::make_unique<lve::LveBuffer>(
            *m_lveDevice, sizeof(MaterialUBO), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buf->Map(); buf->WriteToBuffer(&def);
        VkDescriptorBufferInfo bi = buf->DescriptorInfo();
        lve::LveDescriptorWriter(*m_materialParamSetLayout, *m_materialParamPool)
            .WriteBuffer(0, &bi)
            .Build(m_dummyMatSets[i]);
        m_materialParamBuffers.push_back(std::move(buf));
    }

    m_renderSystem = std::make_unique<RenderSystem>(
        *m_lveDevice, 
        m_lveRenderer->GetSwapChainRenderPass(),
        std::vector<VkDescriptorSetLayout>{
            m_globalSetLayout->GetDescriptorSetLayout(),            // set = 0
            m_materialSetLayout->GetDescriptorSetLayout(),          // set = 1
            m_materialParamSetLayout->GetDescriptorSetLayout()});   // set = 2

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

    std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> submeshMatThisFrame;
    for (auto& kv : m_submeshMatSets) {
        uint32_t objId = kv.first;
        auto& perSubmeshArray = kv.second; // vector<array<VkDescriptorSet, MAX_FRAMES>>
        std::vector<VkDescriptorSet> v(perSubmeshArray.size());
        for (size_t i = 0; i < perSubmeshArray.size(); ++i) {
            v[i] = perSubmeshArray[i][frameIndex];
        }
        submeshMatThisFrame[objId] = std::move(v);
    }

    FrameInfo frameInfo{
        frameIndex,
        m_frameTimeSec,
        commandBuffer,
        *m_lveCamera,
        m_globalDescriptorSets[frameIndex],
        m_objects
    };
    frameInfo.submeshTexSets = &m_submeshTextureSets;     // set=1
    frameInfo.submeshMatSetThisFrame = &submeshMatThisFrame;  // set=2（本帧）
    frameInfo.dummyTexSet = m_defaultTextureSet;
    frameInfo.dummyMatSet = m_dummyMatSets[frameIndex];

    //准备材质参数（set=2）“本帧映射”并赋给 frameInfo ---
    std::unordered_map<uint32_t, VkDescriptorSet> materialParamSetThisFrame;
    for (auto& kv : m_objectMaterialParams) {        // 你给每个对象创建的MaterialGPU容器
        uint32_t objId = kv.first;
        const auto& mgpu = kv.second;                // 包含每帧一个的 UBO 描述符
        materialParamSetThisFrame[objId] = mgpu.sets[frameIndex];
    }
    frameInfo.materialDescriptorSets = &m_objectMaterialSets;
    frameInfo.materialParamSets = &materialParamSetThisFrame;

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
    /*毛坯*/
    std::shared_ptr<LveModel> lveModel = LveModel::CreateModelFromFile(*m_lveDevice, "D:/Data/Study/vulkan/FirstApp/res/models/blank.obj");
    auto blank = LveObject::CreateObject();
    blank.model = lveModel;
    blank.transform.translation = { 0.f, 0.f, 0.f };
    blank.transform.scale = { 1.f, 1.f, 1.f };
    m_objects.emplace(blank.getId(), std::move(blank));

    // set=1：占位材质集
    VkDescriptorSet blankMatSet =
        m_textureManager->GetOrCreateMaterialSet(
            "D:/Data/Study/vulkan/FirstApp/res/textures/TCom_BrushedStainlessSteel_header.jpg",
            /*srgb=*/true);
    m_objectMaterialSets[blank.getId()] = blankMatSet;

    // set=2：材质参数，关闭 baseColor 贴图位（bit0=0），只用因子上色
    MaterialUBO matBlank{};
    matBlank.baseColorFactor = { 1, 1, 1, 1 };    // 这里决定“无贴图”时的颜色
    matBlank.uvTilingOffset = { 1, 1, 0, 0 };
    matBlank.pbrAoAlpha = { 0.0f, 0.5f, 1.0f, 0.0f }; // metal/rough/ao/alphaCut
    matBlank.flags = { 0u, 0u, 0u, 0u };         // ★ bit0=0 -> 不采样 uAlbedo
    AssignMaterialParamsToObject(blank.getId(), matBlank);

    /*砂轮*/
    lveModel = LveModel::CreateModelFromFile(*m_lveDevice, "D:/Data/Study/vulkan/FirstApp/res/models/new_grinding_wheel.obj");
    auto grindingWheel = LveObject::CreateObject();
    grindingWheel.model = lveModel;
    grindingWheel.transform.translation = { 0.f, 0.f, 0.f };
    grindingWheel.transform.scale = { 1.f, 1.f, 1.f };

    uint32_t wheelId = grindingWheel.getId();
    auto subCount = grindingWheel.model->GetSubmeshCount();
    m_submeshTextureSets[wheelId].resize(subCount);
    m_submeshMatSets[wheelId].resize(subCount);
    VkDescriptorSet dsAbrasive = m_textureManager->GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/TCom_OldAluminium_header.jpg", true);
    VkDescriptorSet dsMetal = m_textureManager->GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/Grinding_Wheel.jpg", true);
    m_submeshTextureSets[wheelId][0] = dsAbrasive;
    m_submeshTextureSets[wheelId][1] = dsMetal;

    MaterialUBO mtlAbr{};
    mtlAbr.baseColorFactor = { 1,1,1,1 };
    mtlAbr.uvTilingOffset = { 1,1,0,0 };
    mtlAbr.pbrAoAlpha = { 0.0f, 0.85f, 1.0f, 0.0f }; // 非金属、粗糙高
    mtlAbr.flags = { 1u,0u,0u,0u };

    MaterialUBO mtlMetal{};
    mtlMetal.baseColorFactor = { 1,1,1,1 };
    mtlMetal.uvTilingOffset = { 1,1,0,0 };
    mtlMetal.pbrAoAlpha = { 1.0f, 0.35f, 1.0f, 0.0f }; // 金属、粗糙低
    mtlMetal.flags = { 1u,0u,0u,0u };

    // 为两个 submesh 生成“每帧一套”的 set=2
    CreateMaterialParamSetsForSubmesh(wheelId, 0, mtlAbr);
    CreateMaterialParamSetsForSubmesh(wheelId, 1, mtlMetal);

    m_objects.emplace(grindingWheel.getId(), std::move(grindingWheel));

    /*创建跟随相机的点光源*/
    auto head = LveObject::MakePointLight(.5, 0.25, { 1.f, .86f, .55f });
    // auto head = LveObject::MakePointLight(3., 0.25, { 0.7f, .7f, .7f });
    m_headlightId = head.getId();
    m_objects.emplace(head.getId(), std::move(head));

    /*创建太阳光*/
    CreateSunLight();

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

void FirstApp::CreateSunLight()
{
    glm::vec3 center = glm::vec3(0.f);
    glm::vec3 sunDir = glm::normalize(glm::vec3(-0.3f, 1.0f, -0.2f));
    float D = 200.f;
    float radius = 0.1f;
    glm::vec3 color(1.f, 0.97f, 0.90f);

    const glm::vec3 dirs[] = {
        // 六主轴
        { 1, 0, 0},{-1, 0, 0},{ 0, 1, 0},{ 0,-1, 0},{ 0, 0, 1},{ 0, 0,-1},
        // 八个体对角（立方体顶点）
        { 1, 1, 1},{ 1, 1,-1},{ 1,-1, 1},{ 1,-1,-1},
        {-1, 1, 1},{-1, 1,-1},{-1,-1, 1},{-1,-1,-1},
    };

    int N = sizeof(dirs) / sizeof(dirs[0]);

    m_sunLightIds.clear();
    m_sunLightIds.reserve(N);

    for (int i = 0; i < N; i++) {
        glm::vec3 dir = glm::normalize(dirs[i]);
        auto light = LveObject::MakePointLight(3000, radius, color);
        auto id = light.getId();
        m_sunLightIds.push_back(id);

        light.transform.translation = center - dir * D;
        m_objects.emplace(id, std::move(light));
    }
}


void FirstApp::AssignMaterialParamsToObject(uint32_t objId, const MaterialUBO& init) 
{
    MaterialGPU mgpu{};
    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        mgpu.ubos[i] = std::make_unique<lve::LveBuffer>(
            *m_lveDevice,
            sizeof(MaterialUBO), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        mgpu.ubos[i]->Map();
        mgpu.ubos[i]->WriteToBuffer((void*)&init);

        VkDescriptorBufferInfo bufferInfo = mgpu.ubos[i]->DescriptorInfo();
        lve::LveDescriptorWriter(*m_materialParamSetLayout, *m_materialParamPool)
            .WriteBuffer(0, &bufferInfo)                      // set=2, binding=0
            .Build(mgpu.sets[i]);
    }
    m_objectMaterialParams[objId] = std::move(mgpu);
}

void FirstApp::UpdateMaterialParamsPerFrame(uint32_t objId, int frameIndex, const MaterialUBO& data) 
{
    auto it = m_objectMaterialParams.find(objId);
    if (it == m_objectMaterialParams.end()) return;
    it->second.ubos[frameIndex]->WriteToBuffer((void*)&data);
}

void FirstApp::CreateMaterialParamSetsForSubmesh(uint32_t objId, uint32_t submeshIndex, const MaterialUBO& u) 
{
    auto& slot = m_submeshMatSets[objId][submeshIndex];
    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        auto buf = std::make_unique<lve::LveBuffer>(
            *m_lveDevice, sizeof(MaterialUBO), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buf->Map();
        buf->WriteToBuffer((void*)&u);
        VkDescriptorBufferInfo bi = buf->DescriptorInfo();
        lve::LveDescriptorWriter(*m_materialParamSetLayout, *m_materialParamPool)
            .WriteBuffer(0, &bi)
            .Build(slot[i]);
        m_materialParamBuffers.push_back(std::move(buf));
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
    m_textureManager.reset();

    if (m_lveDevice) {
        vkDeviceWaitIdle(m_lveDevice->device());
        if (m_sharedSampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_lveDevice->device(), m_sharedSampler, nullptr);
            m_sharedSampler = VK_NULL_HANDLE;
        }
    }
}

}