#include "FluteAnalyzer.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "../Global.h"
#include "Logger.h"

using namespace lve;

namespace flute
{

FluteAnalyzer::FluteAnalyzer(lve::LveDevice& device) : m_lveDevice(device)
{
    CreateComputeResources();
    CreateComputePipelineLayout();
    CreateComputePipelines();
}

FluteAnalyzer::~FluteAnalyzer()
{
    if (m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_lveDevice.device(), m_computePipelineLayout, nullptr);
    }
}

bool FluteAnalyzer::ParseFlutePointSet(const std::string& filePath)
{
    PROFILE_SCOPE("FluteAnalyzer::ParseFlutePointSet");

    m_points.clear();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        ERROR("Failed to open file: %s", filePath.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // 跳过注释行、new seg、Plane 等无效行
        if (line.front() == '#' || line.find("new seg") != std::string::npos ||
            line.find("Plane") != std::string::npos) {
            continue;
        }

        // 将逗号替换为空格，方便流读取
        for (char& ch : line) {
            if (ch == ',') ch = ' ';
        }

        std::stringstream ss(line);
        float x, y;
        if (ss >> x >> y) {
            m_points.emplace_back(x, y);
        }
    }

    file.close();

    if (m_points.size() > MAX_FLUTE_POINTS) {
        INFO("Flute point count %d exceeds MAX_FLUTE_POINTS(%u), truncating",
             static_cast<int>(m_points.size()),
             MAX_FLUTE_POINTS);
        m_points.resize(MAX_FLUTE_POINTS);
    }

    INFO("Parse flute points file succeed: total points size: %d",
         static_cast<int>(m_points.size()));
    return !m_points.empty();
}

void FluteAnalyzer::CreateComputeResources()
{
    // 上传 staging buffer（host visible）
    m_stagingBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_FLUTE_POINTS,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_stagingBuffer->Map();

    // binding 1: 轮廓点
    m_pointsBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_FLUTE_POINTS,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // binding 2: 点数计数器
    m_counterBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(uint32_t),
        1,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // binding 3: 计算结果（host visible，直接读回）
    m_resultBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(FluteGeometryResult),
        1,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_resultBuffer->Map();

    // binding 4: KNN 拓扑表（每个点 4 个邻居）
    m_knnBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(uint32_t),
        MAX_FLUTE_POINTS * 4,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // binding 5: TipInfo（shader 声明但未使用，占位）
    m_tipInfoBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(uint32_t),
        2,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // binding 6: 排序中间缓冲
    m_tempSortedBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_FLUTE_POINTS,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // binding 7: 最终有序点
    m_finalPointsBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_FLUTE_POINTS,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 描述符集布局（无 binding 0 采样器，纯计算）
    m_computeSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    m_computePool = LveDescriptorPool::Builder(m_lveDevice)
                        .SetMaxSets(1)
                        .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7)
                        .Build();

    auto pointsInfo = m_pointsBuffer->DescriptorInfo();
    auto counterInfo = m_counterBuffer->DescriptorInfo();
    auto resultInfo = m_resultBuffer->DescriptorInfo();
    auto knnInfo = m_knnBuffer->DescriptorInfo();
    auto tipInfo = m_tipInfoBuffer->DescriptorInfo();
    auto tempSortedInfo = m_tempSortedBuffer->DescriptorInfo();
    auto finalPointsInfo = m_finalPointsBuffer->DescriptorInfo();

    LveDescriptorWriter(*m_computeSetLayout, *m_computePool)
        .WriteBuffer(1, &pointsInfo)
        .WriteBuffer(2, &counterInfo)
        .WriteBuffer(3, &resultInfo)
        .WriteBuffer(4, &knnInfo)
        .WriteBuffer(5, &tipInfo)
        .WriteBuffer(6, &tempSortedInfo)
        .WriteBuffer(7, &finalPointsInfo)
        .Build(m_computeSet);
}

void FluteAnalyzer::CreateComputePipelineLayout()
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(FluteComputePushConstants);

    VkDescriptorSetLayout computeSetLayout = m_computeSetLayout->GetDescriptorSetLayout();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &computeSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(m_lveDevice.device(),
                               &pipelineLayoutInfo,
                               nullptr,
                               &m_computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create flute compute pipeline layout!");
    }
}

void FluteAnalyzer::CreateComputePipelines()
{
    PipelineConfigInfo configInfo{};
    configInfo.pipelineLayout = m_computePipelineLayout;

    m_sortPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_sort_contour.comp.spv",
        configInfo);

    m_tracePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_trace.comp.spv",
        configInfo);

    m_alignPipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_align.comp.spv",
        configInfo);

    m_rakePipeline = std::make_unique<LvePipeline>(
        m_lveDevice,
        "../../../res/shaders/spv/shader_rake_angle.comp.spv",
        configInfo);
}

bool FluteAnalyzer::Analyze(FluteGeometryResult& outResult)
{
    if (m_points.empty()) {
        ERROR("No flute points to analyze");
        return false;
    }

    const uint32_t pointCount = static_cast<uint32_t>(m_points.size());

    // 上传点到 staging buffer
    std::memcpy(m_stagingBuffer->GetMappedMemory(),
                m_points.data(),
                pointCount * sizeof(glm::vec2));

    VkCommandBuffer cmd = m_lveDevice.beginSingleTimeCommands();

    // 拷贝点到设备端 buffer
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = pointCount * sizeof(glm::vec2);
    vkCmdCopyBuffer(cmd,
                    m_stagingBuffer->GetBuffer(),
                    m_pointsBuffer->GetBuffer(),
                    1,
                    &copyRegion);

    // 写入点数
    vkCmdFillBuffer(cmd, m_counterBuffer->GetBuffer(), 0, VK_WHOLE_SIZE, pointCount);

    // 初始化结果 buffer（coreRadiusSqBits = 0xFFFFFFFF 作为无有效结果哨兵）
    FluteGeometryResult initResult{};
    initResult.coreRadiusSqBits = 0xFFFFFFFF;
    vkCmdUpdateBuffer(cmd,
                      m_resultBuffer->GetBuffer(),
                      0,
                      sizeof(FluteGeometryResult),
                      &initResult);

    // transfer -> compute 屏障，确保上传的数据对计算着色器可见
    VkBufferMemoryBarrier uploadBarriers[3]{};
    for (int i = 0; i < 3; i++) {
        uploadBarriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        uploadBarriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        uploadBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        uploadBarriers[i].size = VK_WHOLE_SIZE;
    }
    uploadBarriers[0].buffer = m_pointsBuffer->GetBuffer();
    uploadBarriers[1].buffer = m_counterBuffer->GetBuffer();
    uploadBarriers[2].buffer = m_resultBuffer->GetBuffer();

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         3,
                         uploadBarriers,
                         0,
                         nullptr);

    FluteComputePushConstants push{};
    push.maxPoints = MAX_FLUTE_POINTS;
    push.planeIndex = 0;
    push.isRightCut = 1;

    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_computePipelineLayout,
                            0,
                            1,
                            &m_computeSet,
                            0,
                            nullptr);
    vkCmdPushConstants(cmd,
                       m_computePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(FluteComputePushConstants),
                       &push);

    uint32_t groupCount = (MAX_FLUTE_POINTS + 255) / 256;

    auto insertComputeBarrier = [cmd]() {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
    };

    // KNN 建图
    m_sortPipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, groupCount, 1, 1);
    insertComputeBarrier();

    // 单线程追踪排序
    m_tracePipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, 1, 1, 1);
    insertComputeBarrier();

    // 对齐翻转
    m_alignPipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, groupCount, 1, 1);
    insertComputeBarrier();

    // 前角与槽宽计算
    m_rakePipeline->Bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vkCmdDispatch(cmd, 1, 1, 1);

    m_lveDevice.endSingleTimeCommands(cmd);  // 提交并等待完成

    // 读回结果
    std::memcpy(&outResult, m_resultBuffer->GetMappedMemory(), sizeof(FluteGeometryResult));

    if (outResult.coreRadiusSqBits == 0xFFFFFFFF) {
        INFO("Flute analysis produced no valid result");
        return false;
    }

    return true;
}

}  // namespace flute
