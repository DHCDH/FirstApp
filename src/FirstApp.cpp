#include "FirstApp.h"

#include <array>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

#define GLM_ENABLE_EXPERIMENTAL
#include <gtc/quaternion.hpp>
#include <gtx/quaternion.hpp>

#include "entities/Blank.h"
#include "entities/GrindingWheel.h"
#include "lve/LveBuffer.h"
#include "algorithm/DualNURBSCurveInterpolator.h"

using namespace lve;

struct OrbitConfig {
    float rotateSpeedPerPixel = 0.0020f;  // 每像素约 0.11°
    float panBasePerPixel = 0.0015f;      // 每像素平移尺度 = distance * 该系数
    float dollySpeed = 0.22f;             // 指数缩放强度（滚轮步长）
    float minDistance = 0.1f;            // 缩放最近距离
    float maxDistance = 5000.0f;           // 缩放最远距离
    float minPitch = -1.55334306f;        // -89° (弧度)
    float maxPitch = 1.55334306f;         //  89° (弧度)
    float fovY = 0.87266463f;             //  50° (弧度) 供平移尺度估算
} orbitCfg;

FirstApp::FirstApp(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h,
                   std::string name)
{
    InitLveComponants(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    LoadObjects();
}

void FirstApp::InitLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle,
                                 int w, int h, std::string name)
{
    m_lveWindow =
        std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    m_lveDevice = std::make_unique<LveDevice>(*m_lveWindow);
    m_lveRenderer =
        std::make_unique<LveRenderer>(*m_lveWindow, *m_lveDevice, m_lveDevice->surface());
    m_lveCamera = std::make_unique<LveCamera>();
    m_texture = std::make_unique<LveTexture>(*m_lveDevice,
                                             "D:/Data/Study/vulkan/FirstApp/res/textures/"
                                             "TCom_BrushedStainlessSteel_header.jpg",
                                             true);

    /*global ubo*/
    m_globalPool = LveDescriptorPool::Builder(*m_lveDevice)
                       .SetMaxSets(LveSwapChain::MAX_FRAMES_IN_FLIGHT)
                       .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    LveSwapChain::MAX_FRAMES_IN_FLIGHT)
                       .Build();

    m_uboBuffers.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT);  // 2
    for (int i = 0; i < m_uboBuffers.size(); i++) {
        m_uboBuffers[i] = std::make_unique<LveBuffer>(
            *m_lveDevice,
            sizeof(GlobalUbo),
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_uboBuffers[i]->Map();
    }
    m_globalSetLayout = LveDescriptorSetLayout::Builder(*m_lveDevice)
                            .AddBinding(0,
                                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                        VK_SHADER_STAGE_ALL_GRAPHICS)
                            .Build();
    m_globalDescriptorSets.resize(LveSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < m_globalDescriptorSets.size(); i++) {
        auto bufferInfo = m_uboBuffers[i]->DescriptorInfo();  // binding 0

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
                              .AddBinding(0,
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          VK_SHADER_STAGE_FRAGMENT_BIT)
                              .Build();

    /*创建采样器Sampler*/
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;  // 放大采用线性过滤
    sci.minFilter = VK_FILTER_LINEAR;  // 缩小采用线性过滤
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;  // 重复寻址，适合可平铺贴图
    sci.anisotropyEnable = VK_TRUE;      // 开启各向异性过滤
    sci.maxAnisotropy = 16.0f;
    sci.minLod = 0.0f;
    sci.maxLod = 100.0f;  // 若你生成了 mipmap，这样才能访问所有 mip
    if (vkCreateSampler(m_lveDevice->device(), &sci, nullptr, &m_sharedSampler) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create sampler!");
    }

    m_textureManager = std::make_unique<LveTextureManager>(*m_lveDevice,
                                                           m_sharedSampler,
                                                           *m_materialSetLayout,
                                                           *m_materialPool);

    // 占位 set=1：随便复用一张已有纹理（或你做一张 1x1 白图）
    m_defaultTextureSet = m_textureManager->GetOrCreateMaterialSet(
        "D:/Data/Study/vulkan/FirstApp/res/textures/white_1x1.png",
        true);

    // set = 2，材质参数UBO
    m_materialParamPool = lve::LveDescriptorPool::Builder(*m_lveDevice)
                              .SetMaxSets(128 * lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT)
                              .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           128 * lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT)
                              .Build();

    m_materialParamSetLayout =
        lve::LveDescriptorSetLayout::Builder(*m_lveDevice)
            .AddBinding(0,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_FRAGMENT_BIT)  // 先指向 FS，可先不使用
            .Build();

    MaterialUBO def{};
    def.baseColorFactor = {1, 1, 1, 1};
    def.uvTilingOffset = {1, 1, 0, 0};
    def.pbrAoAlpha = {0.0f, 0.5f, 1.0f, 0.0f};
    def.flags = {0u, 0u, 0u, 0u};

    for (int i = 0; i < lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
        auto buf = std::make_unique<lve::LveBuffer>(
            *m_lveDevice,
            sizeof(MaterialUBO),
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buf->Map();
        buf->WriteToBuffer(&def);
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
            m_globalSetLayout->GetDescriptorSetLayout(),           // set = 0
            m_materialSetLayout->GetDescriptorSetLayout(),         // set = 1
            m_materialParamSetLayout->GetDescriptorSetLayout()});  // set = 2

    m_pointLightSystem =
        std::make_unique<PointLightSystem>(*m_lveDevice,
                                           m_lveRenderer->GetSwapChainRenderPass(),
                                           m_globalSetLayout->GetDescriptorSetLayout());

    m_renderContext = std::make_unique<RenderContext>(*m_lveDevice,
                                                      m_submeshMatSets,
                                                      m_submeshTextureSets,
                                                      m_dummyMatSets,
                                                      *m_textureManager,
                                                      *m_materialParamSetLayout,
                                                      *m_materialParamPool,
                                                      m_materialParamBuffers,
                                                      m_objectMaterialParams);

    m_lastTick = std::chrono::high_resolution_clock::now();
}

void FirstApp::RunFrame()
{
    if (m_lveWindow->WasWindowResized()) {
        m_lveWindow->ResetWindowResizedFlag();
        m_lveRenderer->RecreateSwapChain();
    }

    auto now = std::chrono::high_resolution_clock::now();
    m_frameTimeSec =
        std::chrono::duration<float, std::chrono::seconds::period>(now - m_lastTick)
            .count();
    m_lastTick = now;

    VkCommandBuffer commandBuffer = m_lveRenderer->BeginFrame();
    if (commandBuffer == nullptr) {
        /*此处不做任何阻塞，等待下一次QTimer*/
        return;
    }

    /*设置相机的视图与投影*/
    float aspect = m_lveRenderer->GetAspectRatio();  // 宽高比
    m_lveCamera->SetPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 5000.f);
    //m_lveCamera->SetOrthographicProjection(-1.f, 1.f, -1.f, 1.f, 0.1f, 5000.f);
    // m_lveCamera->SetViewTarget(glm::vec3(0.f, 0.f, -2.f), glm::vec3(0.f, 0.f, 1.f));
    UpdateCameraFromOrbit();

    int frameIndex = m_lveRenderer->GetFrameIndex();

    std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> submeshMatThisFrame;
    for (auto& kv : m_submeshMatSets) {
        uint32_t objId = kv.first;
        auto& perSubmeshArray = kv.second;  // vector<array<VkDescriptorSet, MAX_FRAMES>>
        std::vector<VkDescriptorSet> v(perSubmeshArray.size());
        for (size_t i = 0; i < perSubmeshArray.size(); ++i) {
            v[i] = perSubmeshArray[i][frameIndex];
        }
        submeshMatThisFrame[objId] = std::move(v);
    }

    FrameInfo frameInfo{frameIndex,
                        m_frameTimeSec,
                        commandBuffer,
                        *m_lveCamera,
                        m_globalDescriptorSets[frameIndex],
                        m_objects};
    frameInfo.submeshTexSets = &m_submeshTextureSets;         // set=1
    frameInfo.submeshMatSetThisFrame = &submeshMatThisFrame;  // set=2（本帧）
    frameInfo.dummyTexSet = m_defaultTextureSet;
    frameInfo.dummyMatSet = m_dummyMatSets[frameIndex];

    //准备材质参数（set=2）“本帧映射”并赋给 frameInfo ---
    std::unordered_map<uint32_t, VkDescriptorSet> materialParamSetThisFrame;
    for (auto& kv : m_objectMaterialParams) {  // 你给每个对象创建的MaterialGPU容器
        uint32_t objId = kv.first;
        const auto& mgpu = kv.second;  // 包含每帧一个的 UBO 描述符
        materialParamSetThisFrame[objId] = mgpu.sets[frameIndex];
    }
    frameInfo.materialDescriptorSets = &m_objectMaterialSets;
    frameInfo.materialParamSets = &materialParamSetThisFrame;

    if (m_grindingWheel->GetMotionEnabled()) {
        if (auto it = m_objects.find(m_grindingWheelId); it != m_objects.end()) {
            auto transform = m_grindingWheel->Update(0.016);
            it->second.transform = transform;
        }
    }

    /*更新跟随镜头点光源*/
    glm::vec3 camPos = m_lveCamera->GetPosition();
    if (auto it = m_objects.find(m_headlightId); it != m_objects.end()) {
        it->second.transform.translation = camPos + glm::vec3(15.f, 20.f, 10.f);
        //std::cout << "[Camera]camPos: " << camPos.x << ", " << camPos.y << ", " << camPos.z << "\n";
        float d = m_orbit.distance;
        //it->second.pointLight->lightIntensity = 10.f * d * d;
    }

    /*将PV矩阵写入UBO*/
    GlobalUbo ubo{};
    ubo.projection = m_lveCamera->GetProjection();
    ubo.view = m_lveCamera->GetView();
    ubo.inverseView = m_lveCamera->GetInverseView();
    ubo.ambientLightColor.w = 0.05f;
    m_pointLightSystem->Update(frameInfo, ubo);
    // 自定义视点剔除
    ubo.obsCamPos = glm::vec4(200.f, 0.f, 0.f, 0.f);
    ubo.useCustomCulling = 1;
    m_uboBuffers[frameIndex]->WriteToBuffer(&ubo);

    //BuildGrindingWheelTrackInstances(0.f, 10.f, 10);

    /*进入本帧的主RenderPass*/
    m_lveRenderer->BeginSwapChainRenderPass(commandBuffer);

    /*绘制*/
    m_renderSystem->RenderObjects(frameInfo);
    m_pointLightSystem->Render(frameInfo);

    /*实例化渲染*/
    //RenderGrindingWheelTrack(frameInfo);

    /*结束本帧RenderPass并提交*/
    m_lveRenderer->EndSwapChainRenderPass(commandBuffer);
    m_lveRenderer->EndFrame();
}

void FirstApp::RunFrameForThicknessMap()
{
    if (m_lveWindow->WasWindowResized()) {
        m_lveWindow->ResetWindowResizedFlag();
        m_lveRenderer->RecreateSwapChain();
    }

    VkCommandBuffer commandBuffer = m_lveRenderer->BeginFrame();
    if (commandBuffer == nullptr) return;

    int frameIndex = m_lveRenderer->GetFrameIndex();

    float viewSize = 70.f;  // 根据你的砂轮尺寸调整视口大小
    float aspect = m_lveRenderer->GetAspectRatio();

    m_lveCamera->SetOrthographicProjection(-viewSize * aspect,
                                           viewSize * aspect,
                                           -viewSize,
                                           viewSize,
                                           0.1f,
                                           1000.f);

    m_lveCamera->SetViewTarget(glm::vec3(200.f, 0.f, 0.f),
                               glm::vec3(0.f, -20.f, 0.f),
                               glm::vec3(0.f, -1.f, 0.f));

    GlobalUbo ubo{};
    ubo.projection = m_lveCamera->GetProjection();
    ubo.view = m_lveCamera->GetView();
    // 补齐自定义视点参数，确保 shader_thickness 能获取到正确的摄像机位置
    ubo.obsCamPos = glm::vec4(200.f, 0.f, 0.f, 0.f);
    ubo.useCustomCulling = 1;

    m_uboBuffers[frameIndex]->WriteToBuffer(&ubo);

    FrameInfo frameInfo{frameIndex,
                        0.016f,  // 假定固定 timestep，因为不需要动画
                        commandBuffer,
                        *m_lveCamera,
                        m_globalDescriptorSets[frameIndex],
                        m_objects};

    frameInfo.dummyTexSet = m_defaultTextureSet;
    frameInfo.dummyMatSet = m_dummyMatSets[frameIndex];

    m_lveRenderer->BeginSwapChainRenderPass(commandBuffer);

    m_renderSystem->RenderThicknessMap(frameInfo);

    m_lveRenderer->EndSwapChainRenderPass(commandBuffer);
    m_lveRenderer->EndFrame();
}

void FirstApp::LoadObjects()
{
    /*创建跟随相机的点光源*/
    #if 1
    auto head = LveObject::MakePointLight(1.5f, 0.25, {1.f, 1.f, 1.f});
    // auto head = LveObject::MakePointLight(3., 0.25, { 0.7f, .7f, .7f });
    m_headlightId = head.getId();
    m_objects.emplace(head.getId(), std::move(head));
    #endif

    /*创建太阳光*/
    //CreateSunLight();

    /*毛坯*/
    #if 1
    m_blank = std::make_unique<entity::Blank>(*m_renderContext);
    auto blank = m_blank->CreateObject();
    uint32_t blankId = blank.getId();
    m_objects.emplace(blank.getId(), std::move(blank));
    #endif

    /*砂轮*/
    m_grindingWheel = std::make_unique<entity::GrindingWheel>(*m_renderContext);
    auto grindingWheel = m_grindingWheel->CreateObject();
    m_grindingWheelId = grindingWheel.getId();
    // 生成砂轮扫掠体
    //CreateGrindingWheelTrack(grindingWheel);

    m_objects.emplace(m_grindingWheelId, std::move(grindingWheel));
    std::cout << "[FirstApp] grndWheel id: " << m_grindingWheelId << "\n";

    /*平面*/
    #if 0
    m_plane = std::make_unique<entity::Plane>(*m_renderContext);
    auto plane = m_plane->CreateObject();
    m_planeId = plane.getId();
    m_objects.emplace(m_planeId, std::move(plane));
    std::cout << "[FirstApp] plane id: " << m_planeId << "\n";
    #endif

    // 立方体
    #if 0
    m_cube = std::make_unique<entity::Cube>(*m_renderContext);
    auto cube = m_cube->CreateObject();
    m_cubeId = cube.getId();
    m_objects.emplace(m_cubeId, std::move(cube));
    std::cout << "[FirstApp] cube id: " << m_cubeId << "\n";
    #endif

    // 摄像机
    #if 0
    m_camera = std::make_unique<entity::Camera>(*m_renderContext);
    auto camera = m_camera->CreateObject();
    m_cameraId = camera.getId();
    m_objects.emplace(m_cameraId, std::move(camera));
    std::cout << "[FirstApp] camera id: " << m_cameraId << "\n";
    #endif

    //BuildGrindingWheelTrackInstances(0.f, 10.f, 10);

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
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
        // 八个体对角（立方体顶点）
        {1, 1, 1},
        {1, 1, -1},
        {1, -1, 1},
        {1, -1, -1},
        {-1, 1, 1},
        {-1, 1, -1},
        {-1, -1, 1},
        {-1, -1, -1},
    };

    int N = sizeof(dirs) / sizeof(dirs[0]);

    m_sunLightIds.clear();
    m_sunLightIds.reserve(N);

    for (int i = 0; i < N; i++) {
        glm::vec3 dir = glm::normalize(dirs[i]);
        auto light = LveObject::MakePointLight(6000.f, radius, color);
        auto id = light.getId();
        m_sunLightIds.push_back(id);

        light.transform.translation = center - dir * D;
        m_objects.emplace(id, std::move(light));
    }
}

void FirstApp::UpdateMaterialParamsPerFrame(uint32_t objId, int frameIndex,
                                            const MaterialUBO& data)
{
    auto it = m_objectMaterialParams.find(objId);
    if (it == m_objectMaterialParams.end()) return;
    it->second.ubos[frameIndex]->WriteToBuffer((void*)&data);
}

void FirstApp::BuildGrindingWheelTrackInstances(float t1, float t2, int sampleCount)
{
    //if (m_toolpaths.empty()) {
    //    std::cerr << "[FirstApp] toolpath is empty!"
    //              << "\n";
    //    return;
    //}

    auto it = m_objects.find(m_grindingWheelId);
    if (it == m_objects.end()) return;
    glm::mat4 realBaseMat = it->second.transform.mat4();
    PrintMat4(realBaseMat, "[FirstApp: baseMat]");

    m_grndWheelInstances.clear();
    //m_grindingWheel->CalculateGrindingWheelInstances(m_grndWheelInstances, m_toolpaths);
    m_grindingWheel->CalculateGrindingWheelInstances(m_grndWheelInstances, realBaseMat);

    /*将实例数组上传GPU*/
    VkDeviceSize bufferSize = sizeof(glm::mat4) * m_grndWheelInstances.size();
    m_grndWheelInstanceCount = static_cast<uint32_t>(m_grndWheelInstances.size());
    if (!m_grndWheelInstanceBuffer ||
        m_grndWheelInstanceBuffer->GetBufferSize() < bufferSize) {
        /*重建buffer*/
        std::cout << "rebuild grinding wheel instance buffer"
                  << "\n";
        m_grndWheelInstanceBuffer = std::make_unique<lve::LveBuffer>(
            *m_lveDevice,
            sizeof(glm::mat4),
            m_grndWheelInstanceCount,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    lve::LveBuffer stagingBuffer(
        *m_lveDevice,
        sizeof(glm::mat4),
        m_grndWheelInstanceCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer(m_grndWheelInstances.data());

    m_lveDevice->copyBuffer(stagingBuffer.GetBuffer(),
                            m_grndWheelInstanceBuffer->GetBuffer(),
                            bufferSize);
}

void FirstApp::RenderGrindingWheelTrack(FrameInfo& frameInfo)
{
    frameInfo.instanceBatches.clear();

    /*设置砂轮实例个数*/
    //BuildGrindingWheelTrackInstances(0., 30, 100);

    if (!m_grndWheelInstanceBuffer || m_grndWheelInstanceCount == 0) {
        return;
    }

    auto it = m_objects.find(m_grindingWheelId);
    if (it == m_objects.end()) {
        return;
    }

    auto& grndWheelObj = it->second;
    if (!grndWheelObj.model) {
        return;
    }

    lve::InstanceBatch batch{};
    batch.model = it->second.model.get();  // 绑定砂轮模型(location = 0)
    batch.instanceBuffer = m_grndWheelInstanceBuffer->GetBuffer();
    batch.instanceCount = m_grndWheelInstanceCount;
    batch.instanceStride = sizeof(glm::mat4);

    // （可选）复用砂轮的贴图/材质参数
    if (frameInfo.submeshTexSets) {
        auto itTex = frameInfo.submeshTexSets->find(m_grindingWheelId);
        if (itTex != frameInfo.submeshTexSets->end() && !itTex->second.empty()) {
            batch.set1 = itTex->second[0];  // 获取砂轮第一个子网格的贴图
        }
    }
    if (frameInfo.submeshMatSetThisFrame) {
        auto itMat = frameInfo.submeshMatSetThisFrame->find(m_grindingWheelId);
        if (itMat != frameInfo.submeshMatSetThisFrame->end() && !itMat->second.empty()) {
            batch.set2 = itMat->second[0];  // 获取砂轮专属的材质参数 (包含橘红色和 flag)
        }
    }

    frameInfo.instanceBatches.push_back(batch);
    m_renderSystem->RenderInstances(frameInfo, m_isInstancesShown);
}

int FirstApp::ReadToolPath(std::filesystem::path path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open file: " << path.string() << "\n";
        return -1;
    }

    m_toolpaths.clear();

    ToolPath current;
    bool inSeg = false;  // 读取到new seg才开始收集

    auto flushSeg = [&]() {
        if (!current.points.empty() || !current.normals.empty()) {
            if (current.points.size() != current.normals.size()) {
                std::cerr << "points/normals size mismatch in a segment"
                          << "\n";
                current = ToolPath{};
                return;
            }
            
            current.size = current.points.size();
            m_toolpaths.emplace_back(current);
            current = ToolPath();
        }
        return;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) continue;

        if (line == "new seg") {
            flushSeg();
            inSeg = true;
            continue;
        }

        if (!inSeg) continue;

        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);

        float x, y, z, nx, ny, nz;
        if (!(iss >> x >> y >> z >> nx >> ny >> nz)) {
            std::cerr << "Warning: bad data line (skip): " << line << "\n";
            continue;  // 或者 return -2;
        }

        current.points.push_back({x, y, z});
        current.normals.push_back({nx, ny, nz});
    }

    flushSeg();

    // 插值
    #if 0
    for (int i = 0; i < m_toolpaths.size(); i++) {
        m_toolpaths[i] = DualNURBSCurveInterpolator::Interpolate(m_toolpaths[i], 0.1);

        std::cout << "toolpath[" << i << "].size: " << m_toolpaths[i].size << " \n";
    }
    #endif

    return 0;
}

void FirstApp::CreateGrindingWheelTrack(const lve::LveObject& grindingWheel)
{
    glm::vec3 basePos = grindingWheel.transform.translation;
    glm::quat baseQuat = glm::quat(grindingWheel.transform.rotation);
    auto sharedModel = grindingWheel.model;
    float baseTransparency = grindingWheel.transparency;

    // --- 生成砂轮螺旋扫掠 ---
    int sampleCount = 4;
    float startX = 0.f;  // 这个是相对 X 轴前进的距离起点
    float endX = -15.f;    // 相对前进 100
    float helixAngleDegrees = 45.f;
    //float helixRadius = 5.f;
    float helixRadius = glm::length(glm::vec2(basePos.y, basePos.z));
    float radAngle = glm::radians(helixAngleDegrees);
    float pitch = 2.0f * glm::pi<float>() * helixRadius * glm::tan(radAngle);
    float totalDistance = endX - startX;

    for (int i = 1; i < sampleCount; ++i) {  // 从 1 开始，0 是本体
        float t = static_cast<float>(i) / (sampleCount - 1);
        float currentX = startX + t * totalDistance;

        // 计算当前绕 X 轴应该旋转多少度
        float theta = (currentX / pitch) * 2.0f * glm::pi<float>();

        auto inst = lve::LveObject::CreateObject();
        uint32_t instId = inst.getId();

        inst.model = sharedModel;
        inst.transparency = baseTransparency;

        //inst.neededOutline = true;

        // 构造一个单纯绕 X 轴旋转 theta 度的四元数
        glm::quat rotX = glm::angleAxis(theta, glm::vec3(1.0f, 0.0f, 0.0f));

        // 1. 【姿态计算】：先把本体的朝向绕着 X 轴转 theta 度
        inst.transform.rotation = glm::eulerAngles(rotX * baseQuat);

        // 2. 【位置计算】：先把本体的初始坐标系绕 X 轴旋转，然后再在 X 方向上推远
        // currentX
        glm::vec3 rotatedPos = rotX * basePos;
        inst.transform.translation = rotatedPos + glm::vec3(currentX, 0.0f, 0.0f);

        inst.transform.scale = glm::vec3(1.25f, 1.25f, 1.25f);

        // 克隆材质绑定
        m_submeshTextureSets[instId] = m_submeshTextureSets[m_grindingWheelId];
        m_submeshMatSets[instId] = m_submeshMatSets[m_grindingWheelId];

        m_objects.emplace(instId, std::move(inst));
    }
}

/*******************************************************interaction****************************************************************************/
void FirstApp::UpdateCameraFromOrbit()
{
    // 保证 offset 的长度始终等于要求的距离
    m_orbit.offset = glm::normalize(m_orbit.offset) * m_orbit.distance;
    glm::vec3 camPos = m_orbit.target + m_orbit.offset;

    // 【注意】无死角旋转必须实时更新相机的 Up 向量
    // 请确保你的 LveCamera::SetViewTarget 支持第三个参数（相机的上方向），
    // 默认的 Vulkan 教程一般是支持这个签名的。
    m_lveCamera->SetViewTarget(camPos, m_orbit.target, m_orbit.up);
}

void FirstApp::Orbit(float dxPixels, float dyPixels)
{
    float rotX = dxPixels * orbitCfg.rotateSpeedPerPixel;
    float rotY = dyPixels * orbitCfg.rotateSpeedPerPixel;

    // 1. 获取当前相机的局部坐标系（Screen Space）
    glm::vec3 forward = glm::normalize(-m_orbit.offset);                // 视线方向
    glm::vec3 right = glm::normalize(glm::cross(forward, m_orbit.up));  // 屏幕右方向
    glm::vec3 up = glm::normalize(glm::cross(right, forward));  // 屏幕正上方向

    // 2. 根据鼠标的像素移动生成四元数旋转
    // 上下拖动 (dy)：绕着相机的 Right 轴旋转
    glm::quat qY = glm::angleAxis(rotY, right);
    // 左右拖动 (dx)：绕着相机的 Up 轴旋转（这实现了真正的无死角轨道旋转）
    glm::quat qX = glm::angleAxis(-rotX, up);  // 负号是因为鼠标向右拖，相机应该向左转

    // 合并旋转（注意乘法顺序）
    glm::quat q = qX * qY;

    // 3. 将旋转应用到 offset 和 up 向量上
    m_orbit.offset = q * m_orbit.offset;
    m_orbit.up = q * m_orbit.up;

    UpdateCameraFromOrbit();
}

void FirstApp::Pan(float dxPixels, float dyPixels)
{
    const float panScale = m_orbit.distance * orbitCfg.panBasePerPixel;

    // 基于当前相机的姿态进行平移，确保平移方向与屏幕平行
    glm::vec3 forward = glm::normalize(-m_orbit.offset);
    glm::vec3 right = glm::normalize(glm::cross(forward, m_orbit.up));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // 鼠标右移 -> Target左移；鼠标下移 -> Target上移（根据你的 Vulkan 坐标系调整正负号）
    m_orbit.target += (dxPixels * panScale) * right;
    m_orbit.target += (dyPixels * panScale) * up;

    UpdateCameraFromOrbit();
}

void FirstApp::Dolly(float steps)
{
    const float k = std::exp(-steps * orbitCfg.dollySpeed);  // steps>0 拉近
    m_orbit.distance =
        glm::clamp(m_orbit.distance * k, orbitCfg.minDistance, orbitCfg.maxDistance);
    UpdateCameraFromOrbit();
}

void FirstApp::ResetView()
{
    m_orbit.target = {0.f, 0.f, 2.5f};
    m_orbit.distance = 5.0f;

    // 初始化无死角相机的状态向量
    m_orbit.offset = {0.f, 0.f, -5.0f};  // 假设默认从前向后看
    m_orbit.up = {0.f, 1.f, 0.f};        // 世界坐标系的 Y 为上

    UpdateCameraFromOrbit();
}

void FirstApp::SetCameraPose(glm::vec3 pos, glm::vec3 target, glm::vec3 up)
{
    m_orbit.target = target;
    m_orbit.offset = pos - target;

    m_orbit.distance = glm::length(m_orbit.offset);
    if (m_orbit.distance < orbitCfg.minDistance) m_orbit.distance = orbitCfg.minDistance;

    // 直接使用传入的真实 Up 向量并归一化
    m_orbit.up = glm::normalize(up);

    UpdateCameraFromOrbit();
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