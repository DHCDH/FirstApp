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
    InitializeDataForPSO();
}

void GrindingWheelPoseOptimizer::InitializeDataForPSO()
{
    PROFILE_SCOPE("Initialize Data For PSO");

    auto integrator = std::make_unique<NumericalIntegrator>();
    integrator->SetStrategy(
        std::make_unique<AdaptiveSimpsonStrategy>(1e-10, 1e-10, 1e-2, 1e-12, 0.5));

    m_arcProjectionSolver.SetIntegrator(std::move(integrator))
        .SetCutterParameters(CutterParameters{})
        .SetGrindingWheelParameters(GrindingWheelParameters{})
        .SetPlane(Plane{});

    // m_poseData = arcProjectionSolver.CalculateGrindingWheelPose();
    m_poseConstants = m_arcProjectionSolver.PrepareConstantsForGPU(0.);
    m_swarm.resize(SWARM_SIZE);
    m_arcProjectionSolver.InitializeSwarm(m_swarm, 0.);

    std::cout << "[Optimizer] Generated " << m_swarm.size() << " particles.\n";
}

void GrindingWheelPoseOptimizer::InsertComputeBarrier(VkCommandBuffer cmd)
{
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         1,
                         &memoryBarrier,
                         0,
                         nullptr,
                         0,
                         nullptr);
}

void GrindingWheelPoseOptimizer::RunOptimization(
    OptimizeResourceContext& context, OptimizeMaskRenderSystem& maskRenderSystem,
    const SliceViewConfig& viewConfig, Plane plane,
    BatchedWheelPushConstants wheelPushData)
{
    PROFILE_SCOPE("Run Optimization");

    context.GetParticleBuffer()->Map();
    context.GetParticleBuffer()->WriteToBuffer((void*)m_swarm.data());

    context.GetBestResultSSBOBuffer()->Map();
    BestResultData* mappedBest =
        (BestResultData*)context.GetBestResultSSBOBuffer()->GetMappedMemory();
    mappedBest[0].score = -999999.0f;  // 强制赋予极低分
    mappedBest[0].coreRadius = std::numeric_limits<float>::max();  // u0c复用coreRadius
    mappedBest[0].slotAngle = 0.0f;     // lambda复用slotAngle

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

    PolarPushConstants polarPush{};
    polarPush.planeNormal = plane.normal;
    polarPush.planePoint = plane.point;
    polarPush.planeU = {1., 0., 0.};
    polarPush.planeV = {0., 1., 0.};
    polarPush.stepX = wheelPushData.stepX;
    polarPush.tanHelixAngle = wheelPushData.tanHelixAngle;
    polarPush.radius = wheelPushData.radius;
    polarPush.stepsPerPose = wheelPushData.stepsPerPose;
    polarPush.numTriangles = static_cast<uint32_t>(triangles.size());
    polarPush.curBatchSize = static_cast<uint32_t>(m_swarm.size());
    polarPush.rt1 = m_poseConstants.rt1;
    polarPush.u1 = m_poseConstants.u1;
    polarPush.nt = m_poseConstants.nt;
    polarPush.gR = m_poseConstants.gR;
    polarPush.gr1 = m_poseConstants.gr1;

    std::cout << "[Debug] curBatchSize: " << polarPush.curBatchSize << "\n";


    // --- 申请持久化的指令缓冲 ---
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_lveDevice.getCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdCompute;
    vkAllocateCommandBuffers(m_lveDevice.device(), &allocInfo, &cmdCompute);
    vkResetCommandBuffer(cmdCompute, 0);

    RENDERDOC_START;

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmdCompute, &beginInfo);

    const int PSOIterations = 1;
    for (int iter = 0; iter < PSOIterations; iter++) {
        maskRenderSystem.ComputePolarPSOUpdate(cmdCompute,
                                          context.GetContourDescriptorSet(),
                                          polarPush);
        InsertComputeBarrier(cmdCompute);

        // --- 清空ZMap ---
        VkDeviceSize totalSize = context.GetZMapBuffer()->GetBufferSize();
        VkDeviceSize halfSize = totalSize / 2;
        vkCmdFillBuffer(cmdCompute,
                        context.GetZMapBuffer()->GetBuffer(),
                        0,
                        halfSize,
                        0xFFFFFFFF);
        vkCmdFillBuffer(cmdCompute,
                        context.GetZMapBuffer()->GetBuffer(),
                        halfSize,
                        halfSize,
                        0);

        InsertComputeBarrier(cmdCompute);

        maskRenderSystem.ComputePolarIntersect(cmdCompute,
                                               context.GetContourDescriptorSet(),
                                               polarPush);
        InsertComputeBarrier(cmdCompute);

        maskRenderSystem.ComputePolarEvaluate(cmdCompute,
                                              context.GetContourDescriptorSet(),
                                              polarPush);
        InsertComputeBarrier(cmdCompute);

        maskRenderSystem.ComputePolarReduce(cmdCompute,
                                            context.GetContourDescriptorSet(),
                                          polarPush);
        InsertComputeBarrier(cmdCompute);
    }

    vkEndCommandBuffer(cmdCompute);

    // 直接向 Graphics 队列提交任务 (Vulkan图形队列天然支持计算)
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdCompute;
    vkQueueSubmit(m_lveDevice.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    // 依然死等 GPU 算完这一批，再进入下一次循环
    vkQueueWaitIdle(m_lveDevice.graphicsQueue());

    RENDERDOC_END;

    ReadBackBestResult(context);
}

void GrindingWheelPoseOptimizer::ReadBackBestResult(const OptimizeResourceContext& context)
{
    // 1. 获取映射后的指针 (由于前面有 vkQueueWaitIdle，此时 CPU 读到的绝对是最新数据)
    Particle* particles =
        static_cast<Particle*>(context.GetParticleBuffer()->GetMappedMemory());
    BestResultData* gBest = static_cast<BestResultData*>(
        context.GetBestResultSSBOBuffer()->GetMappedMemory());

    if (!particles || !gBest) {
        std::cerr << "[Error] Buffers not mapped! Cannot read back results.\n";
        return;
    }

    // 2. 直接从 gBest[0] 中读取我们在 reduce.comp 中辛苦选出的全场总冠军
    float bestScore = gBest[0].score;
    float coreRadius = gBest[0].coreRadius;
    float slotAngle = gBest[0].slotAngle;
    uint32_t bestIdx = gBest[0].bestPoseIdx;

    // 3. 通过 bestIdx 反查对应粒子的最佳参数 (x: u0c, y: lambda)
    glm::vec2 bestParams(0.0f);
    if (bestIdx < SWARM_SIZE) {
        bestParams.x = particles[bestIdx].pBestData.x;  // u0c
        bestParams.y = particles[bestIdx].pBestData.y;  // lambda
    }

    m_bestScore = bestScore;

    // ==========================================
    // 🌟 控制台输出：格式化展示优化结果
    // ==========================================
    std::cout << "\n======================================================\n";
    std::cout << "         GPU PSO Succeed (PSO Iterations: 20)      \n";
    std::cout << "======================================================\n";
    if (bestScore <= -900000.0f) {
        std::cout << "[Warning] Failed to find a valid cutting pose, optimization may "
                     "have degenerated!\n";
        std::cout << "This usually means the envelope points of all particle rays did "
                     "not cut into the specified core thickness range.\n";
    } else {
        std::cout << "  -> Global Best Score        : " << bestScore << "\n";
        std::cout << "  -> Best Particle Index      : " << bestIdx << "\n";
        std::cout << "  -> Core Radius              : " << coreRadius << " mm\n";
        std::cout << "  -> Slot Angle               : " << slotAngle << " deg\n";
        std::cout << "  -> Core Parameter [u0c]     : " << bestParams.x << " mm\n";
        std::cout << "  -> Core Parameter [lambda]   : " << bestParams.y << " rad ("
                  << glm::degrees(bestParams.y) << " deg)\n";
    }
    std::cout << "======================================================\n\n";
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