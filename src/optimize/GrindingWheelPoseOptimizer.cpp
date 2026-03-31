#include "GrindingWheelPoseOptimizer.h"

#include <fstream>
#include <iomanip>

#include "../Global.h"
#include "ArcProjectionSolver.h"
#include "RenderDocHelper.h"

using namespace lve;

namespace optimize
{
GrindingWheelPoseOptimizer::GrindingWheelPoseOptimizer(lve::LveDevice& device,
                                                       lve::LveModel& blank,
                                                       lve::LveModel& grndWheel)
    : m_lveDevice(device), m_blank(blank), m_grndWheel(grndWheel)
{
    // InitSSBOResources();
    CalculateTransformMatrixes();
}

void GrindingWheelPoseOptimizer::InitSSBOResources()
{
    uint32_t bufferSize = sizeof(glm::mat4) * BATCH_LAYER_COUNT;
    m_poseSSBOBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::mat4),
        BATCH_LAYER_COUNT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,  // 作为 SSBO 使用
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 映射到 CPU 内存，永不 Unmap，方便后续高速装填
    m_poseSSBOBuffer->Map();

    // 创建 DescriptorSet 布局 (对应 set = 1, binding = 1)
    m_SSBOSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(1,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .Build();

    m_descriptorPool = LveDescriptorPool::Builder(m_lveDevice)
                           .SetMaxSets(1)
                           .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
                           .Build();

    // 3. 写入描述符
    auto bufferInfo = m_poseSSBOBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_SSBOSetLayout, *m_descriptorPool)
        .WriteBuffer(1, &bufferInfo)
        .Build(m_SSBODescriptorSet);
}

void GrindingWheelPoseOptimizer::CalculateTransformMatrixes()
{
    PROFILE_SCOPE("CalculateTransformMatrixes");

    auto integrator = std::make_unique<NumericalIntegrator>();
    integrator->SetStrategy(
        std::make_unique<AdaptiveSimpsonStrategy>(1e-10, 1e-10, 1e-2, 1e-12, 0.5));
    ArcProjectionSolver arcProjectionSolver;
    arcProjectionSolver.SetIntegrator(std::move(integrator))
        .SetCutterParameters(CutterParameters{})
        .SetGrindingWheelParameters(GrindingWheelParameters{})
        .SetPlane(Plane{});

    m_poseData = arcProjectionSolver.CalculateGrindingWheelPose();

    std::cout << "[Optimizer] Generated " << m_poseData.size() << " poses.\n";
}

CameraData GrindingWheelPoseOptimizer::CalculateMicroCamera(
    const BBoxData& bbox, const SliceViewConfig& macroConfig, uint32_t texWidth,
    uint32_t texHeight, const Plane& plane)
{
    CameraData cameraData;

    glm::vec3 up = glm::vec3{0.f, 1.f, 0.f};
    // 如果法线刚好也是 Y 轴，为了防止万向节死锁，换一个上向量
    if (std::abs(glm::dot(plane.normal, up)) > 0.999f) {
        up = glm::vec3(0.f, 0.f, 1.f);
    }
    glm::mat4 viewMatrix = glm::lookAt(plane.point + plane.normal, plane.point, up);

    if (bbox.minX == 0xFFFFFFFF || bbox.maxX <= bbox.minX || bbox.maxY <= bbox.minY) {
        // BBox无效，返回宏观相机
        LveCamera fallbackCam;
        fallbackCam.SetOrthographicProjection(macroConfig.xMin,
                                              macroConfig.xMax,
                                              macroConfig.zMax,
                                              macroConfig.zMin,
                                              -4000.f,
                                              4000.f);
        cameraData.projView = fallbackCam.GetProjection() * viewMatrix;
        cameraData.mapInfo = {macroConfig.xMin,
                              macroConfig.zMax,
                              (macroConfig.xMax - macroConfig.xMin) / texWidth,
                              (macroConfig.zMin - macroConfig.zMax) / texHeight};

        return cameraData;
    }

    // 2. 宏观像素 -> 宏观物理坐标
    float macroDx = (macroConfig.xMax - macroConfig.xMin) / static_cast<float>(texWidth);
    float macroDz = (macroConfig.zMin - macroConfig.zMax) / static_cast<float>(texHeight);

    float worldMinX = macroConfig.xMin + bbox.minX * macroDx;
    float worldMaxX = macroConfig.xMin + bbox.maxX * macroDx;
    float worldMinZ = macroConfig.zMax + bbox.minY * macroDz;
    float worldMaxZ = macroConfig.zMax + bbox.maxY * macroDz;

    // 3. 算出中心点和物理半径，并加上 1.2 倍的安全边缘缓冲
    float centerX = (worldMinX + worldMaxX) * 0.5f;
    float centerZ = (worldMinZ + worldMaxZ) * 0.5f;
    float halfX = std::abs(worldMaxX - worldMinX) * 0.5f;
    float halfZ = std::abs(worldMaxZ - worldMinZ) * 0.5f;

    float maxHalf = std::max({halfX, halfZ, 0.5f}) * 1.2f;
    float aspect = static_cast<float>(texWidth) / static_cast<float>(texHeight);

    float newXMin = centerX - maxHalf * aspect;
    float newXMax = centerX + maxHalf * aspect;
    float newZMin = centerZ - maxHalf;
    float newZMax = centerZ + maxHalf;

    // 4. 生成极限局部放大的 Projection 矩阵和 mapInfo
    LveCamera microCam;
    microCam
        .SetOrthographicProjection(newXMin, newXMax, newZMax, newZMin, -4000.f, 4000.f);

    cameraData.projView = microCam.GetProjection() * viewMatrix;
    cameraData.mapInfo = {newXMin,
                          newZMax,
                          (newXMax - newXMin) / texWidth,
                          (newZMin - newZMax) / texHeight};

    return cameraData;
}

void GrindingWheelPoseOptimizer::RunOptimization(
    OptimizeResourceContext& context, OptimizeMaskRenderSystem& maskRenderSystem,
    const SliceViewConfig& viewConfig, Plane plane,
    BatchedWheelPushConstants wheelPushData)
{
    PROFILE_SCOPE("Run Optimization");

    uint32_t totalPoses = static_cast<uint32_t>(m_poseData.size());
    if (totalPoses == 0) return;

    std::cout << "[Optimizer] Starting GPU evaluation loop for " << totalPoses
              << " poses..."
              << "\n ";

    std::vector<Triangle> triangles = ExtractTriangles(m_grndWheel);
    std::cout << "[Debug] Extracted Triangles Count: " << triangles.size() << "\n";
    VkDeviceSize bufferSize = triangles.size() * sizeof(Triangle);

    // 1. 创建临时的 Staging Buffer (CPU 可见)
    lve::LveBuffer stagingBuffer(
        m_lveDevice,
        sizeof(Triangle),
        triangles.size(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 2. 映射并把数据写入 Staging Buffer
    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)triangles.data());
    // 注意：LveBuffer 析构时会自动 Unmap

    // 3. 将数据从 Staging Buffer 拷贝到 GPU 专属的 Triangle Buffer
    m_lveDevice.copyBuffer(stagingBuffer.GetBuffer(),
                           context.GetTriangleBuffer()->GetBuffer(),
                           bufferSize);

    // 构造截面局部基向量
    glm::vec3 up = (std::abs(plane.normal.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                      : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 planeU = glm::normalize(glm::cross(plane.normal, up));
    glm::vec3 planeV = glm::cross(plane.normal, planeU);

    PolarPushConstants push{};
    push.planeNormal = plane.normal;
    push.planePoint = plane.point;
    push.planeU = planeU;
    push.planeV = planeV;
    push.stepX = wheelPushData.stepX;
    push.tanHelixAngle = wheelPushData.tanHelixAngle;
    push.radius = wheelPushData.radius;
    push.stepsPerPose = wheelPushData.stepsPerPose;
    push.numTriangles = static_cast<uint32_t>(triangles.size());

    // --- 映射读回内存 ---
    context.GetBestResultSSBOBuffer()->Map();
    BestResultData* mappedResults =
        (BestResultData*)context.GetBestResultSSBOBuffer()->GetMappedMemory();

    // --- 申请持久化的指令缓冲 ---
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_lveDevice.getCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdCompute;
    vkAllocateCommandBuffers(m_lveDevice.device(), &allocInfo, &cmdCompute);

    for (uint32_t i = 0; i < totalPoses; i += BATCH_LAYER_COUNT) {
        std::cout << "\r[Optimizer] Processing batch: " << i << " / " << totalPoses
                  << " (" << (i * 100 / totalPoses) << "%) completed..." << std::flush;

        uint32_t curBatchSize = std::min((uint32_t)BATCH_LAYER_COUNT, totalPoses - i);

        // 将BATCH_LAYER_COUNT数量的位姿矩阵填进SSBO
        memcpy(context.GetPoseSSBOBuffer()->GetMappedMemory(),
               m_poseData.data() + i,
               curBatchSize * sizeof(glm::mat4));

        if (i == 0) RENDERDOC_START;

        vkResetCommandBuffer(cmdCompute, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdCompute, &beginInfo);

        VkDeviceSize halfSize = BATCH_LAYER_COUNT * ZMAP_RESOLUTION * sizeof(uint32_t);

        // 前半部分，存储theta对应minR
        float initR = 1000.0f;
        uint32_t initBits = std::bit_cast<uint32_t>(initR);
        vkCmdFillBuffer(cmdCompute,
                        context.GetZMapBuffer()->GetBuffer(),
                        0,
                        halfSize,
                        initBits);

        // 后半部分，存储theta对应maxR
        float zeroR = 0.f;
        uint32_t zeroBits = std::bit_cast<uint32_t>(zeroR);
        vkCmdFillBuffer(cmdCompute,
                        context.GetZMapBuffer()->GetBuffer(),
                        halfSize,
                        halfSize,
                        zeroBits);

#if 0
        ResultData initResult{};
        initResult.coreRadiusSqBits = 0xFFFFFFFF;
        std::vector<ResultData> initResults(BATCH_LAYER_COUNT, initResult);
        vkCmdUpdateBuffer(cmdCompute,
                          context.GetResultBuffer()->GetBuffer(),
                          0,
                          sizeof(ResultData) * BATCH_LAYER_COUNT,
                          initResults.data());
#endif

        VkMemoryBarrier clearBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        clearBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmdCompute,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &clearBarrier,
                             0,
                             nullptr,
                             0,
                             nullptr);

        maskRenderSystem.ComputePolarIntersect(cmdCompute,
                                               context.GetContourDescriptorSet(),
                                               push);

        // 屏障：等待交集计算完成
        VkMemoryBarrier intersectBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        intersectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        intersectBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmdCompute,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &intersectBarrier,
                             0,
                             nullptr,
                             0,
                             nullptr);

        maskRenderSystem.ComputePolarEvaluate(cmdCompute,
                                              context.GetContourDescriptorSet(),
                                              push);

        push.curBatchSize = curBatchSize;

        // --- evaluate与reduce之间的屏障 ---
        VkMemoryBarrier reduceBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        reduceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        reduceBarrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmdCompute,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &reduceBarrier,
                             0,
                             nullptr,
                             0,
                             nullptr);

        maskRenderSystem.ComputePolarReduce(cmdCompute,
                                            context.GetContourDescriptorSet(),
                                            push);

        vkEndCommandBuffer(cmdCompute);

        // 直接向 Graphics 队列提交任务 (Vulkan图形队列天然支持计算)
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdCompute;

        vkQueueSubmit(m_lveDevice.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

        // 依然死等 GPU 算完这一批，再进入下一次循环
        vkQueueWaitIdle(m_lveDevice.graphicsQueue());

        BestResultData batchBest = mappedResults[0];

        if (batchBest.bestResult.score > m_bestScore &&
            batchBest.bestResult.coreRadiusSqBits != 0xFFFFFFFF) {
            m_bestScore = batchBest.bestResult.score;
            m_bestResult = batchBest.bestResult;
            m_bestPose = m_poseData[i + batchBest.bestPoseIdx].modelMatrix;
        }

        if (i == 0) RENDERDOC_END;
    }

    vkFreeCommandBuffers(m_lveDevice.device(),
                         m_lveDevice.getCommandPool(),
                         1,
                         &cmdCompute);

    std::cout << "\n[Optimizer] All batches finished successfully!"
              << "\n";

    std::cout << "\n========== [ Result ] Best Result ==========\n";
    float sq = std::bit_cast<float>(m_bestResult.coreRadiusSqBits);
    std::cout << "core radius: " << std::sqrt(sq) << "\n";
    std::cout << "slot angle: " << m_bestResult.slotAngle << "\n";
    std::cout << "score: " << m_bestResult.score << "\n";
    // std::cout << "rake angle: " << m_bestResult.rakeAngle << "\n";

    std::cout << "\n========== [ Result ] Best Pose Matrix ==========\n";
    for (int row = 0; row < 4; ++row) {
        std::cout << "[ ";
        for (int col = 0; col < 4; ++col) {
            // 注意：glm::mat4 的索引是 mat[col][row]
            std::cout << m_bestPose[col][row] << "\t";
        }
        std::cout << "]\n";
    }
    std::cout << "Best Score: " << m_bestScore << "\n";
    std::cout << "=================================================\n\n";

    context.GetBestResultSSBOBuffer()->Unmap();

    WriteToolPath();

    std::cout << "BATCH_LAYER_COUNT : " << BATCH_LAYER_COUNT << "\n";
}

float GrindingWheelPoseOptimizer::EvaluateFitness(const ResultData& result)
{
    if (result.coreRadiusSqBits == 0xFFFFFFFF) {
        return -999999.0f;
    }

    float coreRadiusSq = std::bit_cast<float>(result.coreRadiusSqBits);
    float coreRadius = std::sqrt(coreRadiusSq);

    if (coreRadius > 5.f) {
        return -999999.0f;
    }

    float targetSlotAngle = 65.0f;
    float targetArcLength = 5.f * glm::radians(targetSlotAngle);
    float targetCoreRadius = 3.0f;
    // float targetRakeAngle = 10.0f;

    float resultArcLength = glm::radians(result.slotAngle) * 5.f;

    float slotAngleError = std::abs(resultArcLength - targetArcLength);
    float coreRadiusError = std::abs(coreRadius - targetCoreRadius);
    // ArcProjection已经的计算已经保证了前角和螺旋角
    // float rakeAngleError = std::abs(result.rakeAngle - targetRakeAngle);

    float weightSlot = 1.f;
    float weightCore = 1.f;

    float score = -(slotAngleError * weightSlot + coreRadiusError * weightCore);

    return score;
}

void GrindingWheelPoseOptimizer::WriteToolPath()
{
    std::string filename =
        "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\optimize_toolpath.txt";

    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing!\n";
        return;
    }

    outFile << "new seg\n";

    outFile << std::fixed << std::setprecision(6);

    int num = 30;
    float stepX = 0.2f;
    for (int stepIndex = 0; stepIndex < num; ++stepIndex) {
        // --- 1. 计算位移和旋转角 (与 Vertex Shader 保持绝对一致) ---
        float stepDist = static_cast<float>(stepIndex) * stepX;
        float theta = (stepDist * tan(glm::radians(30.f))) / 5.f;

        // --- 2. 构建平移矩阵 ---
        glm::mat4 transX(1.0f);
        transX[3][0] = stepDist;

        // --- 3. 构建绕X轴旋转矩阵 ---
        // 注意 GLM 默认是列主序 [col][row]
        glm::mat4 rotX(1.0f);
        rotX[1][1] = std::cos(theta);
        rotX[1][2] = std::sin(theta);
        rotX[2][1] = -std::sin(theta);
        rotX[2][2] = std::cos(theta);

        // --- 4. 计算当前步的最终位姿 ---
        glm::mat4 finalPose = (transX * rotX) * m_bestPose;

        // --- 5. 提取坐标 (位置) 和 法向量 (X轴) ---
        glm::vec3 p = glm::vec3(finalPose[3]);  // 提取位移向量
        glm::vec3 n = glm::vec3(finalPose[0]);  // 提取局部坐标系的X轴作为法向

        // --- 6. 写入文件 ---
        outFile << p.x << "," << p.y << "," << p.z << "," << n.x << "," << n.y << ","
                << n.z << "\n";
    }

    outFile.close();

    std::cout << "[System] Successfully exported tool path to: " << filename << "\n";
}

std::vector<Triangle> GrindingWheelPoseOptimizer::ExtractTriangles(
    const lve::LveModel& model)
{
    std::vector<Triangle> triangles;

    // 假设你的模型有获取 builder 或原始顶点的方法
    // 这里以常见的 Vulkan 模型数据结构为例：
    const auto& vertices = model.GetVertices();
    const auto& indices = model.GetIndices();

    if (!indices.empty()) {
        for (size_t i = 0; i < indices.size(); i += 3) {
            Triangle tri;
            // 注意：w 分量设为 1.0，方便做矩阵乘法
            tri.v0 = glm::vec4(vertices[indices[i + 0]].position, 1.0f);
            tri.v1 = glm::vec4(vertices[indices[i + 1]].position, 1.0f);
            tri.v2 = glm::vec4(vertices[indices[i + 2]].position, 1.0f);
            triangles.push_back(tri);
        }
    } else {
        // 如果没有索引缓冲
        for (size_t i = 0; i < vertices.size(); i += 3) {
            Triangle tri;
            tri.v0 = glm::vec4(vertices[i + 0].position, 1.0f);
            tri.v1 = glm::vec4(vertices[i + 1].position, 1.0f);
            tri.v2 = glm::vec4(vertices[i + 2].position, 1.0f);
            triangles.push_back(tri);
        }
    }
    return triangles;
}

GrindingWheelPoseOptimizer::~GrindingWheelPoseOptimizer()
{
}

}  // namespace optimize