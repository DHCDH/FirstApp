#include "SliceView.h"

using namespace lve;

SliceView::SliceView(lve::LveDevice& device, const SliceViewConfig& config,
    void* nativeWindowHandle, void* nativeInstanceHandle, 
    int w, int h, std::string name)
 : m_device(device), m_viewConfig(config)
{
    m_window = std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);
    m_camera = std::make_unique<LveCamera>();
    m_cameraUboBuffer = std::make_unique<LveBuffer>(m_device,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_cameraUboBuffer->Map();
    m_setLayout = LveDescriptorSetLayout::Builder(m_device)
        .AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
        .Build();
    m_descriptorPool = LveDescriptorPool::Builder(m_device)
        .SetMaxSets(1)
        .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
        .Build();
    auto bufferInfo = m_cameraUboBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_setLayout, *m_descriptorPool)
        .WriteBuffer(0, &bufferInfo)
        .Build(m_descriptorSet);

    CreateContactMaskResources();
    CreateReadbackBuffer();

    m_sliceMaskRenderSystem = std::make_unique<SliceMaskRenderSystem>(m_device,
        m_maskRenderPass,
        m_setLayout->GetDescriptorSetLayout()
    );

    m_lastTick = std::chrono::high_resolution_clock::now();
}

void SliceView::RunFrame()
{
    if (!m_window) return;

    if (!m_blankModel) return;

    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
    }

    auto now = std::chrono::high_resolution_clock::now();
    m_frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(now - m_lastTick).count();
    m_lastTick = now;
}

void SliceView::CreateSingleMaskResource(VkImage& image, VkDeviceMemory& memory,
    VkImageView& view, VkFramebuffer& framebuffer)
{
    assert(image == VK_NULL_HANDLE && "Image resource logic error: Old image not destroyed!");
    assert(framebuffer == VK_NULL_HANDLE && "Framebuffer resource logic error: Old framebuffer not destroyed!");

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_UINT;
    imageInfo.extent.width = m_viewConfig.nX;
    imageInfo.extent.height = m_viewConfig.nZ;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | 
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device.device(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device.device(), image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_device.findMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device.device(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate mask image memory!");
    }
    vkBindImageMemory(m_device.device(), image, memory, 0);

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
    if (vkCreateImageView(m_device.device(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    /*创建Framebuffer*/
    VkImageView attachments[] = { view };
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_maskRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = m_viewConfig.nX;
    framebufferInfo.height = m_viewConfig.nZ;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device.device(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create mask framebuffer!");
    }

}

void SliceView::CreateContactMaskResources()
{
    std::cout << "Enter function: " << __FUNCTION__ << "\n";
    std::cout << "----nX: " << m_viewConfig.nX << " nZ: " << m_viewConfig.nZ << "\n";
    /*创建共享RenderPass*/
    if (m_maskRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R32_UINT; // 对应ContactMaskCode
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // pass结束时，要把attachment的内容保存下来
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(m_device.device(), &renderPassInfo, nullptr, &m_maskRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create mask render pass!");
        }
    }

    /*棒料资源*/
    CreateSingleMaskResource(m_blankMaskImage, m_blankMaskMemory, m_blankMaskView, m_blankMaskFramebuffer);
    /*砂轮资源*/
    CreateSingleMaskResource(m_grndWheelMaskImage, m_grndWheelMaskMemory, m_grndWheelMaskView, m_grndWheelMaskFramebuffer);
    /*交集资源*/
    CreateSingleMaskResource(m_contactMaskImage, m_contactMaskMemory, m_contactMaskView, m_contactMaskFramebuffer);
}

/*在GPU创建一块将contactMask图像拷贝到CPU的缓冲区*/
void SliceView::CreateReadbackBuffer()
{
    VkDeviceSize size = m_viewConfig.nX * m_viewConfig.nZ * sizeof(uint32_t);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(m_device.device(), &bufferInfo, nullptr, &m_readbackBuffer);

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(m_device.device(), m_readbackBuffer, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = m_device.findMemoryType(memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(m_device.device(), &allocateInfo, nullptr, &m_readbackMemory);
    vkBindBufferMemory(m_device.device(), m_readbackBuffer, m_readbackMemory, 0);

}

void SliceView::BuildContactMask(const SliceFrameData& frameData)
{
    UpdateGrindingWheelInstanceBuffer(frameData.wheelModels);

    UpdateSliceCamera(frameData.yM);
    std::cout << "------------------frameData.yM: " << frameData.yM << "\n";

    VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

    /*配置离屏Render Pass*/
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_maskRenderPass;
    renderPassInfo.framebuffer = m_blankMaskFramebuffer;

    std::cout << "---nX: " << m_viewConfig.nX << " nZ: " << m_viewConfig.nZ << "\n";

    /*设置渲染区域*/
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { m_viewConfig.nX, m_viewConfig.nZ };

    VkClearValue clearValue{};
    clearValue.color.uint32[0] = 0u;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    VkImageMemoryBarrier initBarrier{};
    initBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    initBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    initBarrier.image = m_blankMaskImage;
    initBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    initBarrier.srcAccessMask = 0;
    initBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    /*执行渲染*/
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    /*设置视口*/
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_viewConfig.nX);
    viewport.height = static_cast<float>(m_viewConfig.nZ);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    /*设置裁剪矩形*/
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { m_viewConfig.nX, m_viewConfig.nZ };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (m_blankModel && m_sliceMaskRenderSystem) {
        SliceInfo info{
            commandBuffer,
            *m_blankModel,
            frameData.blankModel,
            m_descriptorSet,
            frameData.yM,
            frameData.thickness
        };
        m_sliceMaskRenderSystem->RenderBlank(info);
    }
    else {
        throw std::runtime_error("m_blankModel or m_sliceMaskRenderSystem is nullptr");
    }

    if (m_grndWheelModel && m_sliceMaskRenderSystem && m_grndWheelInstanceCount > 0) {
        SliceInstancedInfo info{
            commandBuffer,
            *m_grndWheelModel,
            m_grndWheelInstanceBuffer->GetBuffer(),
            m_grndWheelInstanceCount,
            m_descriptorSet,
            frameData.yM,
            frameData.thickness
        };
        m_sliceMaskRenderSystem->RenderGrindingWheelInstances(info);
    }
    else if (!m_grndWheelModel) {
        throw std::runtime_error("m_grndWheelModel is nullptr");
    }
    else if (!m_sliceMaskRenderSystem) {
        throw std::runtime_error("m_sliceMaskRenderSystem is nullptr");
    }
    else if (m_grndWheelInstanceCount == 0) {
        throw std::runtime_error("m_grndWheelInstanceCount == 0");
    }

    /*结束RenderPass*/
    vkCmdEndRenderPass(commandBuffer);

    /*拷贝图像到Readback buffer*/
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = m_blankMaskImage;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { m_viewConfig.nX, m_viewConfig.nZ, 1 };
    vkCmdCopyImageToBuffer(commandBuffer, m_blankMaskImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readbackBuffer, 1, &region);

    // 恢复 Layout (可选，为了严谨)
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    //使用 m_device 提交
    m_device.endSingleTimeCommands(commandBuffer);
}

QImage SliceView::GetSerializedImage()
{
    void* data;
    vkMapMemory(m_device.device(), m_readbackMemory, 0, VK_WHOLE_SIZE, 0, &data);

    uint32_t* rawPixels = static_cast<uint32_t*>(data);
    int w = m_viewConfig.nX;
    int h = m_viewConfig.nZ;
    QImage image(w, h, QImage::Format_ARGB32);

    int noneZeroCount = 0;

    for (int i = 0; i < w * h; ++i) {
        uint32_t val = rawPixels[i];
        QRgb color = qRgb(30, 30, 30); // 默认背景

        if (val != 0) {
            noneZeroCount++;
            if (noneZeroCount < 5) {
                std::cout << "Found non-zero pixel at index " << i << ": " << val << "\n";
            }
        }

        if (val == 1u) color = qRgb(0, 255, 0); // 绿色棒料
        if (val == 2u) color = qRgb(255, 0, 0); // 红色砂轮
        if (val == 3u) color = qRgb(0, 0, 255); // 蓝色交集

        // 简单的像素设置，可能有性能优化空间，但用于调试足够
        int x = i % w;
        int y = i / w;
        image.setPixel(x, y, color);
    }

    std::cout << "==== DEBUG: Total Non-Zero Pixels: " << noneZeroCount << " ====\n";

    vkUnmapMemory(m_device.device(), m_readbackMemory);
    return image;
}

void SliceView::UpdateGrindingWheelInstanceBuffer(const std::vector<glm::mat4>& instances)
{
    m_grndWheelInstanceCount = static_cast<uint32_t>(instances.size());
    if (m_grndWheelInstanceCount == 0) {
        std::cout << "Instance count: " << m_grndWheelInstanceCount << "\n";
        return;
    }

    VkDeviceSize bufferSize = sizeof(glm::mat4) * m_grndWheelInstanceCount;
    uint32_t instanceSize = sizeof(glm::mat4);

    if (m_grndWheelInstanceBuffer == VK_NULL_HANDLE ||
        m_grndWheelInstanceBuffer->GetInstanceCount() < m_grndWheelInstanceCount) {
        /*现有缓冲过小，重建缓冲*/
        m_grndWheelInstanceBuffer = std::make_unique<LveBuffer>(m_device,
            instanceSize,
            m_grndWheelInstanceCount,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    /*使用staging buffer提高性能*/
    lve::LveBuffer stagingBuffer(m_device,
        sizeof(InstanceData),
        m_grndWheelInstanceCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)instances.data());
    stagingBuffer.Flush();

    VkCommandBuffer copyCmd = m_device.beginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(copyCmd, stagingBuffer.GetBuffer(), m_grndWheelInstanceBuffer->GetBuffer(), 1, &copyRegion);

    m_device.endSingleTimeCommands(copyCmd);
}

void SliceView::UpdateSliceViewConfig(const SliceViewConfig& config)
{
    bool needRecreate = (config.nX != m_viewConfig.nX) || (config.nZ != m_viewConfig.nZ);
    
    m_viewConfig = config;

    if (needRecreate) {
        vkDeviceWaitIdle(m_device.device());

        /*销毁旧资源*/
        CleanupMaskResource(m_blankMaskImage, m_blankMaskView, m_blankMaskMemory, m_blankMaskFramebuffer);
        CleanupMaskResource(m_grndWheelMaskImage, m_grndWheelMaskView, m_grndWheelMaskMemory, m_grndWheelMaskFramebuffer);
        CleanupMaskResource(m_contactMaskImage, m_contactMaskView, m_contactMaskMemory, m_contactMaskFramebuffer);

        /*销毁读回缓冲*/
        CleanupReadbackResource();

        /*重新创建资源*/
        CreateContactMaskResources();

        /*重新创建读回缓存*/
        CreateReadbackBuffer();
    }
}

void SliceView::CleanupMaskResource(VkImage& image, VkImageView& view, 
    VkDeviceMemory& memory, VkFramebuffer& framebuffer)
{
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device.device(), framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device.device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device.device(), image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device.device(), memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void SliceView::CleanupReadbackResource()
{
    if (m_readbackBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device.device(), m_readbackBuffer, nullptr);
        m_readbackBuffer = VK_NULL_HANDLE;
    }
    if (m_readbackMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device.device(), m_readbackMemory, nullptr);
        m_readbackMemory = VK_NULL_HANDLE;
    }
}

void SliceView::UpdateSliceCamera(const float& sliceHeight)
{
    const auto& p = m_viewConfig;

    float centralX = 0.5f * (p.xMin + p.xMax);
    float centralZ = 0.5f * (p.zMin + p.zMax);

    float cameraHeight = 100.f; //相机在Y轴高度
    glm::vec3 cameraPos{ centralX, sliceHeight + cameraHeight, centralZ };
    glm::vec3 target{centralX, sliceHeight, centralZ};
    glm::vec3 up{0.f, 0.f, 1.f};

    m_camera->SetViewTarget(cameraPos, target, up);
    m_camera->SetOrthographicProjection(p.xMin, p.xMax, p.zMax, p.zMin, 0.01f, cameraHeight + 100.f);

    GlobalUbo ubo{};
    ubo.projection = m_camera->GetProjection();
    ubo.view = m_camera->GetView();
    ubo.inverseView = m_camera->GetInverseView();
    ubo.ambientLightColor = glm::vec4{ 0.f };

    m_cameraUboBuffer->WriteToBuffer(&ubo);
    m_cameraUboBuffer->Flush();
}

void SliceView::WaitIdle()
{
    vkDeviceWaitIdle(m_device.device());
}

SliceView::~SliceView()
{
    WaitIdle();
    CleanupReadbackResource();

    if (m_maskRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device.device(), m_maskRenderPass, nullptr);
        m_maskRenderPass = VK_NULL_HANDLE;
    }

    CleanupMaskResource(m_blankMaskImage, m_blankMaskView, m_blankMaskMemory, m_blankMaskFramebuffer);
    CleanupMaskResource(m_grndWheelMaskImage, m_grndWheelMaskView, m_grndWheelMaskMemory, m_grndWheelMaskFramebuffer);
    CleanupMaskResource(m_contactMaskImage, m_contactMaskView, m_contactMaskMemory, m_contactMaskFramebuffer);

}