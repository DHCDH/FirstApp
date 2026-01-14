#include "SliceView.h"

#include <windows.h>
#include <vulkan/vulkan_win32.h>

using namespace lve;

SliceView::SliceView(lve::LveDevice& device, const SliceViewConfig& config,
                     void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h,
                     std::string name)
    : m_device(device), m_viewConfig(config)
{
    m_window =
        std::make_unique<LveWindow>(nativeWindowHandle, nativeInstanceHandle, w, h, name);

    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = static_cast<HINSTANCE>(nativeInstanceHandle);
    createInfo.hwnd = static_cast<HWND>(nativeWindowHandle);
    if (vkCreateWin32SurfaceKHR(m_device.getVkInstance(),
                                &createInfo,
                                nullptr,
                                &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create slice view surface!");
    }

    m_renderer = std::make_unique<LveRenderer>(*m_window, m_device, m_surface);
    m_camera = std::make_unique<LveCamera>();

    InitOffscreenResources();
    InitDisplayResources();
    CreateContactMaskResources();
    RecreateDisplayDescriptorSet();

    m_sliceMaskRenderSystem =
        std::make_unique<SliceMaskRenderSystem>(m_device,
                                                m_maskRenderPass,
                                                m_setLayout->GetDescriptorSetLayout());

    m_sliceMaskRenderSystem->CreateSliceContourPipeline(
        m_renderer->GetSwapChainRenderPass());

    m_lastTick = std::chrono::high_resolution_clock::now();
}

/*初始化离屏渲染*/
void SliceView::InitOffscreenResources()
{
    m_cameraUboBuffer = std::make_unique<LveBuffer>(
        m_device,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_cameraUboBuffer->Map();

    m_setLayout = LveDescriptorSetLayout::Builder(m_device)
                      .AddBinding(0,
                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                  VK_SHADER_STAGE_ALL_GRAPHICS)
                      .Build();
    m_descriptorPool = LveDescriptorPool::Builder(m_device)
                           .SetMaxSets(1)
                           .AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
                           .Build();
    auto bufferInfo = m_cameraUboBuffer->DescriptorInfo();
    LveDescriptorWriter(*m_setLayout, *m_descriptorPool)
        .WriteBuffer(0, &bufferInfo)
        .Build(m_descriptorSet);
}

void SliceView::InitDisplayResources()
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

    if (vkCreateSampler(m_device.device(), &samplerInfo, nullptr, &m_displaySampler) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create display sampler!");
    }

    m_displayPool = LveDescriptorPool::Builder(m_device)
                        .SetMaxSets(10)
                        .AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20)
                        .Build();
    m_displaySetLayout = LveDescriptorSetLayout::Builder(m_device)
                             .AddBinding(0,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_SHADER_STAGE_FRAGMENT_BIT)
                             .AddBinding(1,
                                         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                         VK_SHADER_STAGE_FRAGMENT_BIT)
                             .Build();
    m_displaySystem = std::make_unique<SliceDisplaySystem>(
        m_device,
        m_renderer->GetSwapChainRenderPass(),
        m_displaySetLayout->GetDescriptorSetLayout());
}

/*关联PASS1的图像到PASS2的描述符*/
void SliceView::RecreateDisplayDescriptorSet()
{
    if (m_stencilSampleView == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create descriptor set: Mask view is null");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_stencilSampleView;
    imageInfo.sampler = m_displaySampler;

    // 处理边缘纹理信息
    VkDescriptorImageInfo imageInfoEdge{};
    imageInfoEdge.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfoEdge.imageView = m_blankMaskView;  // ?
    imageInfoEdge.sampler = m_displaySampler;

    lve::LveDescriptorWriter writer(*m_displaySetLayout, *m_displayPool);
    writer.WriteImage(0, &imageInfo);
    writer.WriteImage(1, &imageInfoEdge);

    /*判断是否存在DescriptorSet*/
    if (m_displayDescriptorSet != VK_NULL_HANDLE) {
        writer.Overwrite(m_displayDescriptorSet);
    } else {
        writer.Build(m_displayDescriptorSet);
    }
}

void SliceView::RunFrame()
{
    if (!m_window) return;

    if (!m_blankModel) return;

    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
    }

    auto now = std::chrono::high_resolution_clock::now();
    m_frameTimeSec =
        std::chrono::duration<float, std::chrono::seconds::period>(now - m_lastTick)
            .count();
    m_lastTick = now;
}

void SliceView::CreateSingleMaskResource(VkImage& image, VkDeviceMemory& memory,
                                         VkImageView& view, VkFramebuffer& framebuffer)
{
    assert(image == VK_NULL_HANDLE &&
           "Image resource logic error: Old image not destroyed!");
    assert(framebuffer == VK_NULL_HANDLE &&
           "Framebuffer resource logic error: Old framebuffer not destroyed!");

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
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    allocInfo.memoryTypeIndex =
        m_device.findMemoryType(memRequirements.memoryTypeBits,
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
    std::array<VkImageView, 2> attachments = {view, m_depthStencilView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_maskRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = m_viewConfig.nX;
    framebufferInfo.height = m_viewConfig.nZ;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device.device(), &framebufferInfo, nullptr, &framebuffer) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create mask framebuffer!");
    }
}

void SliceView::CreateContactMaskResources()
{
    std::cout << "Enter function: " << __FUNCTION__ << "\n";
    std::cout << "----nX: " << m_viewConfig.nX << " nZ: " << m_viewConfig.nZ << "\n";

    if (m_depthStencilImage != VK_NULL_HANDLE) {
        std::cout << "WARNING: m_depthStencilImage is NOT NULL initially! It is: "
                  << m_depthStencilImage << "\n";
    } else {
        std::cout << "WARNING: m_depthStencilImage is NULL initially!"
                  << "\n";
    }

    /*创建深度/模板缓冲图像*/
    if (m_depthStencilImage == VK_NULL_HANDLE) {
        VkFormat depthFormat = FindDepthStencilFormat();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_viewConfig.nX;
        imageInfo.extent.height = m_viewConfig.nZ;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        imageInfo.usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;  // 该image不存储颜色，而是挂载到RenderPass上作为深度测试和模板测试的工作区
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        m_device.createImageWithInfo(imageInfo,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                     m_depthStencilImage,
                                     m_depthStencilMemory);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_depthStencilImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        // 关键：同时包含 Depth 和 Stencil
        viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device.device(),
                              &viewInfo,
                              nullptr,
                              &m_depthStencilView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth stencil view!");
        }

        VkImageViewCreateInfo stencilViewInfo = viewInfo;
        stencilViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

        if (vkCreateImageView(m_device.device(),
                              &stencilViewInfo,
                              nullptr,
                              &m_stencilSampleView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create depth stencil sample view!");
        }
    }

    /*创建共享RenderPass*/
    if (m_maskRenderPass == VK_NULL_HANDLE) {
        /*Attachment 0: Color(Mask)*/
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
        depthAttachment.format = FindDepthStencilFormat();
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
        if (vkCreateRenderPass(m_device.device(),
                               &renderPassInfo,
                               nullptr,
                               &m_maskRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create mask render pass!");
        }
    }

    /*棒料资源*/
    CreateSingleMaskResource(m_blankMaskImage,
                             m_blankMaskMemory,
                             m_blankMaskView,
                             m_blankMaskFramebuffer);
    /*砂轮资源*/
    CreateSingleMaskResource(m_grndWheelMaskImage,
                             m_grndWheelMaskMemory,
                             m_grndWheelMaskView,
                             m_grndWheelMaskFramebuffer);
    /*交集资源*/
    CreateSingleMaskResource(m_contactMaskImage,
                             m_contactMaskMemory,
                             m_contactMaskView,
                             m_contactMaskFramebuffer);
}

void SliceView::BuildContactMask(const SliceFrameData& frameData)
{
    if (m_window->WasWindowResized()) {
        m_window->ResetWindowResizedFlag();
        m_renderer->RecreateSwapChain();
    }

    UpdateGrindingWheelInstanceBuffer(frameData.wheelModels);

    UpdateSliceCamera(frameData.yM);

    VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

    /*配置离屏Render Pass*/
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_maskRenderPass;
    renderPassInfo.framebuffer = m_blankMaskFramebuffer;

    /*设置渲染区域*/
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {m_viewConfig.nX, m_viewConfig.nZ};

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0u;
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

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
    scissor.offset = {0, 0};
    scissor.extent = {m_viewConfig.nX, m_viewConfig.nZ};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // 绘制全屏三角形，强制将深度缓冲写入y=yM平面的深度值
    if (m_sliceMaskRenderSystem) {
        m_sliceMaskRenderSystem->BindPlaneInjectionPipeline(commandBuffer);
        m_sliceMaskRenderSystem->RenderPlaneInjection(commandBuffer,
                                                      m_descriptorSet,
                                                      frameData.yM);
    }

    if (m_blankModel) {
        m_sliceMaskRenderSystem->BindBlankStencilPipeline(commandBuffer);
        SliceInfo info{commandBuffer,
                       *m_blankModel,
                       frameData.blankModel,
                       m_descriptorSet,
                       frameData.yM,
                       0.f};
        m_sliceMaskRenderSystem->RenderBlank(info);
    }

    if (m_grndWheelModel && m_grndWheelInstanceCount > 0) {
        SliceInstancedInfo instInfo{commandBuffer,
                                    *m_grndWheelModel,
                                    m_grndWheelInstanceBuffer->GetBuffer(),
                                    m_grndWheelInstanceCount,
                                    m_descriptorSet,
                                    frameData.yM,
                                    0.};

        /*绘制砂轮前表面*/
        m_sliceMaskRenderSystem->BindGrindingWheelStencilFrontPipeline(commandBuffer);
        m_sliceMaskRenderSystem->RenderGrindingWheelInstances(instInfo);

        /*绘制砂轮后表面*/
        m_sliceMaskRenderSystem->BindGrindingWheelStencilBackPipeline(commandBuffer);
        m_sliceMaskRenderSystem->RenderGrindingWheelInstances(instInfo);

        //if (m_isWireFrame && m_grndWheelModel) {
        //    m_sliceMaskRenderSystem->BindSliceContourPipeline(commandBuffer);
        //    m_sliceMaskRenderSystem->RenderSliceContour(instInfo);
        //}
    } else if (!m_grndWheelModel) {
        throw std::runtime_error("m_grndWheelModel is nullptr");
    } else if (!m_sliceMaskRenderSystem) {
        throw std::runtime_error("m_sliceMaskRenderSystem is nullptr");
    } else if (m_grndWheelInstanceCount == 0) {
        throw std::runtime_error("m_grndWheelInstanceCount == 0");
    }

    /*结束RenderPass*/
    vkCmdEndRenderPass(commandBuffer);

    std::vector<VkImageMemoryBarrier> barriers;

    VkImageMemoryBarrier stencilBarrier{};
    stencilBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    stencilBarrier.image = m_depthStencilImage;
    stencilBarrier.subresourceRange =
        {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
    stencilBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    stencilBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    stencilBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    stencilBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers.push_back(stencilBarrier);

    if (m_blankMaskImage != VK_NULL_HANDLE) {
        VkImageMemoryBarrier colorBarrier{};
        colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        colorBarrier.image = m_blankMaskImage;  // 这是 Attachment 0
        colorBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // RenderPass 结束时，由于 finalLayout 设置，它已经是 COLOR_ATTACHMENT_OPTIMAL
        colorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Display Shader 需要 SHADER_READ_ONLY_OPTIMAL
        colorBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers.push_back(colorBarrier);
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,    
        0,
        0,
        nullptr,
        0,
        nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    //使用 m_device 提交
    m_device.endSingleTimeCommands(commandBuffer);

    /*屏上显示*/
    if (auto drawCmd = m_renderer->BeginFrame()) {
        m_renderer->BeginSwapChainRenderPass(drawCmd);
        m_displaySystem->Render(drawCmd,
                                m_displayDescriptorSet,
                                m_viewConfig.nX,
                                m_viewConfig.nZ,
                                m_isWireFrame);

        if (m_isWireFrame && m_grndWheelModel && m_grndWheelInstanceCount > 0) {
            SliceInstancedInfo onscreenInstInfo{drawCmd,
                                                *m_grndWheelModel,
                                                m_grndWheelInstanceBuffer->GetBuffer(),
                                                m_grndWheelInstanceCount,
                                                m_descriptorSet,
                                                frameData.yM,
                                                0.};
            m_sliceMaskRenderSystem->BindSliceContourPipeline(drawCmd);
            m_sliceMaskRenderSystem->RenderSliceContour(onscreenInstInfo);
        }

        m_renderer->EndSwapChainRenderPass(drawCmd);
        m_renderer->EndFrame();
    }
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
        m_grndWheelInstanceBuffer = std::make_unique<LveBuffer>(
            m_device,
            instanceSize,
            m_grndWheelInstanceCount,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    /*使用staging buffer提高性能*/
    lve::LveBuffer stagingBuffer(
        m_device,
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
    vkCmdCopyBuffer(copyCmd,
                    stagingBuffer.GetBuffer(),
                    m_grndWheelInstanceBuffer->GetBuffer(),
                    1,
                    &copyRegion);

    m_device.endSingleTimeCommands(copyCmd);
}

void SliceView::UpdateSliceViewConfig(const SliceViewConfig& config)
{
    bool needRecreate = (config.nX != m_viewConfig.nX) || (config.nZ != m_viewConfig.nZ);

    m_viewConfig = config;

    if (needRecreate) {
        vkDeviceWaitIdle(m_device.device());

        /*销毁旧资源*/
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

        /*清理深度缓冲*/
        if (m_depthStencilView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device.device(), m_depthStencilView, nullptr);
            m_depthStencilView = VK_NULL_HANDLE;
        }
        if (m_stencilSampleView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device.device(), m_stencilSampleView, nullptr);
            m_stencilSampleView = VK_NULL_HANDLE;
        }
        if (m_depthStencilImage != VK_NULL_HANDLE) {
            vkDestroyImage(m_device.device(), m_depthStencilImage, nullptr);
            m_depthStencilImage = VK_NULL_HANDLE;
        }
        if (m_depthStencilMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device.device(), m_depthStencilMemory, nullptr);
            m_depthStencilMemory = VK_NULL_HANDLE;
        }

        CreateContactMaskResources();
        RecreateDisplayDescriptorSet();
    }
}

void SliceView::UpdateSliceCamera(const float& sliceHeight)
{
    const auto& p = m_viewConfig;

    float centralHoriz = 0.5f * (p.xMin + p.xMax);  // 对应 World Z 中心
    float centralVert = 0.5f * (p.zMin + p.zMax);   // 对应 World Y 中心

    float physicalHeight = p.zMax - p.zMin;
    VkExtent2D extent = m_window->GetExtent();
    float aspectRatio =
        (extent.height > 0) ? (float)extent.width / (float)extent.height : 1.0f;
    float physicalWidth = physicalHeight * aspectRatio;

    float adjustedHorizMin = centralHoriz - physicalWidth * 0.5f;
    float adjustedHorizMax = centralHoriz + physicalWidth * 0.5f;

    float safeCeiling = 2000.f;  // 假设棒料最长不超过2000
    glm::vec3 cameraPos{safeCeiling, centralVert, centralHoriz};
    glm::vec3 target{0.f, centralVert, centralHoriz};
    glm::vec3 up{0.f, 1.f, 0.f};

    /*添加微小偏移，防止yM为物体底面时因为浮点误差导致底面闪烁*/
    float epsilon = 0.001f;

    float farPlaneDist = 4000.f;
    m_camera->SetViewTarget(cameraPos, target, up);
    m_camera->SetOrthographicProjection(adjustedHorizMin,
                                        adjustedHorizMax,
                                        p.zMax,
                                        p.zMin,
                                        0.01f,
                                        farPlaneDist);

    GlobalUbo ubo{};
    ubo.projection = m_camera->GetProjection();
    ubo.view = m_camera->GetView();
    ubo.inverseView = m_camera->GetInverseView();
    ubo.ambientLightColor = glm::vec4{0.f};

    m_cameraUboBuffer->WriteToBuffer(&ubo);
    m_cameraUboBuffer->Flush();
}

/*查询显卡支持的缓存格式*/
VkFormat SliceView::FindDepthStencilFormat()
{
    return m_device.findSupportedFormat(
        {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

void SliceView::WaitIdle()
{
    vkDeviceWaitIdle(m_device.device());
}

SliceView::~SliceView()
{
    WaitIdle();

    m_sliceMaskRenderSystem.reset();
    m_displaySystem.reset();
    m_renderer.reset();

    if (m_displaySampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device.device(), m_displaySampler, nullptr);
        m_displaySampler = VK_NULL_HANDLE;
    }

    if (m_maskRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device.device(), m_maskRenderPass, nullptr);
        m_maskRenderPass = VK_NULL_HANDLE;
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

    if (m_depthStencilView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device.device(), m_depthStencilView, nullptr);
    }
    if (m_stencilSampleView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device.device(), m_stencilSampleView, nullptr);
    }
    if (m_depthStencilImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device.device(), m_depthStencilImage, nullptr);
    }
    if (m_depthStencilMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device.device(), m_depthStencilMemory, nullptr);
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_device.getVkInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}