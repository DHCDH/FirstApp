#include "SliceResourceContext.h"

#include <stdexcept>

#include "LveFrameInfo.h"
#include "SliceGlobal.h"

using namespace lve;

namespace slice
{

SliceResourceContext::SliceResourceContext(LveDevice& lveDevice, uint32_t width,
                                           uint32_t height)
    : m_lveDevice(lveDevice), m_width(width), m_height(height)
{
    m_depthStencilFormat = m_lveDevice.findSupportedFormat(
        {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    CreateSampler();
    CreateGlobalResources();
    CreateRenderPass();
    CreateOffscreenImage();
    CreateFramebuffers();
    CreateComputeResources();
    CreateUnitGridBuffer();
}

void SliceResourceContext::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    if (vkCreateSampler(m_lveDevice.device(), &samplerInfo, nullptr, &m_maskSampler) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create display sampler!");
    }

    return;
}

void SliceResourceContext::CreateGlobalResources()
{
    m_cameraUboBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_cameraUboBuffer->Map();

    m_globalSetLayout = LveDescriptorSetLayout::Builder(m_lveDevice)
                            .AddBinding(0,
                                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                        VK_SHADER_STAGE_ALL_GRAPHICS)
                            .Build();
    m_globalPool = LveDescriptorPool::Builder(m_lveDevice)
                       .SetMaxSets(1)
                       .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
                       .Build();
    auto bufferInfo = m_cameraUboBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_globalSetLayout, *m_globalPool)
        .WriteBuffer(0, &bufferInfo)
        .Build(m_globalDescriptorSet);

    return;
}

void SliceResourceContext::CreateRenderPass()
{
    // Attachment 0: Color(Mask)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R32_UINT;  // 对应ContactMaskCode
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;  // pass结束时，要把attachment的内容保存下来
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    /*Attachment 1: Depth/Stencil*/
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthStencilFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Depth Clear
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp =
        VK_ATTACHMENT_LOAD_OP_CLEAR;  // Stencil Clear (关键：每帧清零)
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;  // 索引为 1
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;  // 绑定深度模板

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                          depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    if (vkCreateRenderPass(m_lveDevice.device(),
                           &renderPassInfo,
                           nullptr,
                           &m_maskRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mask render pass!");
    }

    return;
}

void SliceResourceContext::CreateOffscreenImage()
{
    // 棒料资源
    CreateSingleImageResource(m_blankMaskImage,
                              m_blankMaskMemory,
                              m_blankMaskView,
                              m_blankMaskFramebuffer);
    // 砂轮资源
    CreateSingleImageResource(m_grndWheelMaskImage,
                              m_grndWheelMaskMemory,
                              m_grndWheelMaskView,
                              m_grndWheelMaskFramebuffer);
    // 交集资源
    CreateSingleImageResource(m_contactMaskImage,
                              m_contactMaskMemory,
                              m_contactMaskView,
                              m_contactMaskFramebuffer);

    // 创建深度图
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthStencilFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    imageInfo.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;  // 该image不存储颜色，而是挂载到RenderPass上作为深度测试和模板测试的工作区
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    m_lveDevice.createImageWithInfo(imageInfo,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    m_depthStencilImage,
                                    m_depthStencilMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthStencilImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthStencilFormat;
    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_lveDevice.device(),
                          &viewInfo,
                          nullptr,
                          &m_depthStencilView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth stencil view!");
    }

    VkImageViewCreateInfo stencilViewInfo = viewInfo;
    stencilViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (vkCreateImageView(m_lveDevice.device(),
                          &stencilViewInfo,
                          nullptr,
                          &m_stencilSampleView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth stencil sample view!");
    }

    VkCommandBuffer transitionCmd = m_lveDevice.beginSingleTimeCommands();
    auto transitionImage =
        [&](VkImage img, VkImageAspectFlags aspect, VkImageLayout newLayout) {
            if (img == VK_NULL_HANDLE) return;

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = img;
            barrier.subresourceRange.aspectMask = aspect;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(transitionCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);
        };

    // 转换所有颜色掩码图
    transitionImage(m_blankMaskImage,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transitionImage(m_grndWheelMaskImage,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transitionImage(m_contactMaskImage,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // 转换深度图 (可选，但为了防止外部采样也一并转换)
    transitionImage(m_depthStencilImage,
                    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_lveDevice.endSingleTimeCommands(transitionCmd);
}

void SliceResourceContext::CreateSingleImageResource(VkImage& image,
                                                     VkDeviceMemory& memory,
                                                     VkImageView& view,
                                                     VkFramebuffer& framebuffer)
{
    assert(image == VK_NULL_HANDLE &&
           "Image resource logic error: Old image not destroyed!");
    assert(framebuffer == VK_NULL_HANDLE &&
           "Framebuffer resource logic error: Old framebuffer not destroyed!");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_UINT;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    m_lveDevice.createImageWithInfo(imageInfo,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    image,
                                    memory);

    // 创建ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_lveDevice.device(), &viewInfo, nullptr, &view) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return;
}

void SliceResourceContext::CreateFramebuffers()
{
    CreateSingleFramebuffer(m_blankMaskView, m_blankMaskFramebuffer);
    CreateSingleFramebuffer(m_grndWheelMaskView, m_grndWheelMaskFramebuffer);
    CreateSingleFramebuffer(m_contactMaskView, m_contactMaskFramebuffer);
}

void SliceResourceContext::CreateSingleFramebuffer(VkImageView colorView,
                                                   VkFramebuffer& framebuffer)
{
    std::array<VkImageView, 2> attachments = {colorView, m_depthStencilView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_maskRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_width;
    framebufferInfo.height = m_height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_lveDevice.device(),
                            &framebufferInfo,
                            nullptr,
                            &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
    }
}

void SliceResourceContext::CreateComputeResources()
{
    // 创建坐标存储buffer
    m_contourPointsBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_POINTS * MAX_PLANES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // 创建计数器
    m_counterBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                                  sizeof(uint32_t),
                                                  MAX_PLANES,
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // GPU计算结果buffer
    m_resultBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(ResultData),
        MAX_PLANES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // --- 获取前角所需buffer ---
    m_knnBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                              sizeof(uint32_t) * 4,
                                              MAX_POINTS * MAX_PLANES,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_tipInfoBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(uint32_t),
        2 * MAX_PLANES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_tempSortedBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                                     sizeof(glm::vec2),
                                                     MAX_POINTS * MAX_PLANES,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_sortedPointsBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(glm::vec2),
        MAX_POINTS * MAX_PLANES,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,  // 最终结果，必须能作为SRC拷贝回CPU
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // --- 包围盒Bbox存储和读回buffer
    m_bboxBuffer = std::make_unique<LveBuffer>(m_lveDevice,
                                               sizeof(BBoxData),
                                               MAX_PLANES,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_bboxReadbackBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        sizeof(BBoxData),
        MAX_PLANES,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    m_bboxComputeSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(0,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    // 创建计算描述符集布局
    m_contourComputeSetLayout =
        LveDescriptorSetLayout::Builder(m_lveDevice)
            .AddBinding(0,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .AddBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
            .Build();

    // 更新描述符池
    m_computeDescriptorPool =
        LveDescriptorPool::Builder(m_lveDevice)
            .SetMaxSets(2)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2)
            .AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8)
            .Build();

    // 绑定资源并构建 Descriptor Set
    auto pointsInfo = m_contourPointsBuffer->DescriptorInfo();
    auto counterInfo = m_counterBuffer->DescriptorInfo();
    auto resultBufferInfo = m_resultBuffer->DescriptorInfo();
    auto knnInfo = m_knnBuffer->DescriptorInfo();
    auto tipInfoBufferInfo = m_tipInfoBuffer->DescriptorInfo();
    auto tempSortedInfo = m_tempSortedBuffer->DescriptorInfo();
    auto finalPointsInfo = m_sortedPointsBuffer->DescriptorInfo();
    auto imageInfo = VkDescriptorImageInfo{m_maskSampler,
                                           m_blankMaskView,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    LveDescriptorWriter(*m_contourComputeSetLayout, *m_computeDescriptorPool)
        .WriteImage(0, &imageInfo)
        .WriteBuffer(1, &pointsInfo)
        .WriteBuffer(2, &counterInfo)
        .WriteBuffer(3, &resultBufferInfo)
        .WriteBuffer(4, &knnInfo)
        .WriteBuffer(5, &tipInfoBufferInfo)
        .WriteBuffer(6, &tempSortedInfo)
        .WriteBuffer(7, &finalPointsInfo)
        .Build(m_contourDescriptorSet);

    auto bboxInfo = m_bboxBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_bboxComputeSetLayout, *m_computeDescriptorPool)
        .WriteImage(0, &imageInfo)  // 共用一张blankMaskView
        .WriteBuffer(1, &bboxInfo)
        .Build(m_bboxDescriptorSet);

    // 创建staging buffer
    VkDeviceSize totalReadbackSize = sizeof(ResultData) * MAX_PLANES +
                                     sizeof(uint32_t) * MAX_PLANES +
                                     sizeof(glm::vec2) * MAX_POINTS * MAX_PLANES;
    m_readbackBuffer = std::make_unique<LveBuffer>(
        m_lveDevice,
        totalReadbackSize,
        1,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    return;
}

// --- 构建UV模板 ---
void SliceResourceContext::CreateUnitGridBuffer()
{
    // 设定网格分辨率 (轴向 x 圆周)
    const uint32_t radialRes = 360;  // 圆周方向采样
    const uint32_t axialRes = 100;   // 轴向(宽度)采样

    std::vector<ParametricVertex> vertices;

    for (uint32_t i = 0; i <= axialRes; i++) {
        float u = static_cast<float>(i) / axialRes;
        for (uint32_t j = 0; j <= radialRes; j++) {
            float v = static_cast<float>(j) / radialRes;
            vertices.push_back({{u, v}});
        }
    }

    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < axialRes; i++) {
        for (uint32_t j = 0; j < radialRes; j++) {
            uint32_t start = i * (radialRes + 1) + j;
            // 第一个三角形
            indices.push_back(start);
            indices.push_back(start + 1);
            indices.push_back(start + radialRes + 1);
            // 第二个三角形
            indices.push_back(start + 1);
            indices.push_back(start + radialRes + 2);
            indices.push_back(start + radialRes + 1);
        }
    }

    m_unitGridVertexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(ParametricVertex) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    // 1. 创建顶点 Staging Buffer 并拷贝数据
    lve::LveBuffer vertexStaging(
        m_lveDevice,
        sizeof(ParametricVertex),
        static_cast<uint32_t>(vertices.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vertexStaging.Map();
    vertexStaging.WriteToBuffer(vertices.data());

    // --- 创建索引 Staging Buffer 并拷贝数据 ---
    lve::LveBuffer indexStaging(
        m_lveDevice,
        sizeof(uint32_t),
        static_cast<uint32_t>(indices.size()),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    indexStaging.Map();
    indexStaging.WriteToBuffer(indices.data());

    // --- 创建设备本地 Buffer ---
    m_unitGridBuffer = std::make_unique<lve::LveBuffer>(
        m_lveDevice,
        sizeof(ParametricVertex),
        static_cast<uint32_t>(vertices.size()),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_unitGridIndexBuffer = std::make_unique<lve::LveBuffer>(
        m_lveDevice,
        sizeof(uint32_t),
        static_cast<uint32_t>(indices.size()),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // --- 执行拷贝命令 ---
    VkCommandBuffer commandBuffer = m_lveDevice.beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer,
                    vertexStaging.GetBuffer(),
                    m_unitGridBuffer->GetBuffer(),
                    1,
                    &copyRegion);

    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(commandBuffer,
                    indexStaging.GetBuffer(),
                    m_unitGridIndexBuffer->GetBuffer(),
                    1,
                    &indexCopyRegion);

    m_lveDevice.endSingleTimeCommands(commandBuffer);
}

void SliceResourceContext::Resize(uint32_t newWidth, uint32_t newHeight)
{
    if (newWidth == m_width && newHeight == m_height) {
        return;
    }

    m_width = newWidth;
    m_height = newHeight;

    vkDeviceWaitIdle(m_lveDevice.device());

    // 销毁旧资源
    CleanupMaskResource(m_blankMaskImage,
                        m_blankMaskView,
                        m_blankMaskMemory,
                        m_blankMaskFramebuffer);
    CleanupMaskResource(m_grndWheelMaskImage,
                        m_grndWheelMaskView,
                        m_grndWheelMaskMemory,
                        m_grndWheelMaskFramebuffer);
    CleanupMaskResource(m_contactMaskImage,
                        m_contactMaskView,
                        m_contactMaskMemory,
                        m_contactMaskFramebuffer);
    VkFramebuffer dummyFramebuffer = VK_NULL_HANDLE;
    CleanupMaskResource(m_depthStencilImage,
                        m_depthStencilView,
                        m_depthStencilMemory,
                        dummyFramebuffer);
    if (m_stencilSampleView) {
        vkDestroyImageView(m_lveDevice.device(), m_stencilSampleView, nullptr);
        m_stencilSampleView = VK_NULL_HANDLE;
    }

    // 重建图像和Framebuffer
    CreateOffscreenImage();
    CreateFramebuffers();

    // 更新Compute Shader的Descriptor Set（因为ImageView变了）
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_maskSampler;
    imageInfo.imageView = m_blankMaskView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    lve::LveDescriptorWriter(*m_contourComputeSetLayout, *m_computeDescriptorPool)
        .WriteImage(0, &imageInfo)
        .Overwrite(m_contourDescriptorSet);

    lve::LveDescriptorWriter(*m_bboxComputeSetLayout, *m_computeDescriptorPool)
        .WriteImage(0, &imageInfo)
        .Overwrite(m_bboxDescriptorSet);
}

void SliceResourceContext::CleanupMaskResource(VkImage& image, VkImageView& view,
                                               VkDeviceMemory& memory,
                                               VkFramebuffer& framebuffer)
{
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_lveDevice.device(), framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_lveDevice.device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(m_lveDevice.device(), image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_lveDevice.device(), memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void SliceResourceContext::UpdateGlobalUbo(void* uboData, size_t size)
{
    m_cameraUboBuffer->WriteToBuffer(uboData, size);
    m_cameraUboBuffer->Flush();
}

SliceResourceContext::~SliceResourceContext()
{
    vkDeviceWaitIdle(m_lveDevice.device());

    if (m_maskRenderPass) {
        vkDestroyRenderPass(m_lveDevice.device(), m_maskRenderPass, nullptr);
    }

    CleanupMaskResource(m_blankMaskImage,
                        m_blankMaskView,
                        m_blankMaskMemory,
                        m_blankMaskFramebuffer);
    CleanupMaskResource(m_grndWheelMaskImage,
                        m_grndWheelMaskView,
                        m_grndWheelMaskMemory,
                        m_grndWheelMaskFramebuffer);
    CleanupMaskResource(m_contactMaskImage,
                        m_contactMaskView,
                        m_contactMaskMemory,
                        m_contactMaskFramebuffer);
    VkFramebuffer dummyFramebuffer = VK_NULL_HANDLE;
    CleanupMaskResource(m_depthStencilImage,
                        m_depthStencilView,
                        m_depthStencilMemory,
                        dummyFramebuffer);
    if (m_stencilSampleView) {
        vkDestroyImageView(m_lveDevice.device(), m_stencilSampleView, nullptr);
        m_stencilSampleView = VK_NULL_HANDLE;
    }

    if (m_maskSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_lveDevice.device(), m_maskSampler, nullptr);
        m_maskSampler = VK_NULL_HANDLE;
    }
}

}  // namespace slice