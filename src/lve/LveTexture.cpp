#include "LveTexture.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdexcept>
#include <cstring>

namespace lve {

    LveTexture::LveTexture(LveDevice& device, const std::string& filepath, bool srgb)
        : m_device{ device } {
        CreateFromFile(filepath, srgb);
    }

    LveTexture::LveTexture(LveDevice& device, const std::string& filepath, VkSampler sharedSmapler, bool srgb)
        : m_device{ device } {
        m_sampler = sharedSmapler;
        m_ownSampler = false;
        CreateFromFile(filepath, srgb);
    }

    VkDescriptorImageInfo LveTexture::DescriptorInfo() const {
        VkDescriptorImageInfo info{};
        info.sampler = m_sampler;
        info.imageView = m_imageView;
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return info;
    }

    void LveTexture::CreateFromFile(const std::string& path, bool srgb) 
    {
        int w, h, comp;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
        if (!pixels) throw std::runtime_error("stbi_load failed: " + path);
        m_width = static_cast<uint32_t>(w);
        m_height = static_cast<uint32_t>(h);
        m_format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        CreateImage(m_width, m_height, m_format);
        // layout: UNDEFINED -> TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        TransitionLayout(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        Upload(pixels, static_cast<VkDeviceSize>(m_width) * m_height * 4, m_width, m_height);
        TransitionLayout(m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        CreateImageView();

        if(m_sampler == VK_NULL_HANDLE)
            CreateSampler();

        stbi_image_free(pixels);
    }

    void LveTexture::CreateImage(uint32_t w, uint32_t h, VkFormat format) 
    {
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { w, h, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        m_device.createImageWithInfo(
            info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_image, m_memory); // 已封装的设备函数
    }

    void LveTexture::CreateImageView() 
    {
        VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ci.image = m_image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = m_format;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device.device(), &ci, nullptr, &m_imageView) != VK_SUCCESS)
            throw std::runtime_error("create image view failed");
    }

    void LveTexture::CreateSampler() 
    {
        VkSamplerCreateInfo ci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        ci.magFilter = VK_FILTER_LINEAR;
        ci.minFilter = VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        ci.addressModeU = ci.addressModeV = ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        ci.anisotropyEnable = VK_TRUE;
        ci.maxAnisotropy = m_device.properties.limits.maxSamplerAnisotropy; // 设备属性里有上限
        ci.minLod = 0.f; 
        ci.maxLod = 0.f;

        if (vkCreateSampler(m_device.device(), &ci, nullptr, &m_sampler) != VK_SUCCESS)
            throw std::runtime_error("create sampler failed");
    }

    void LveTexture::TransitionLayout(VkImage image, VkImageLayout oldL, VkImageLayout newL) 
    {
        VkCommandBuffer cmd = m_device.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = oldL;
        barrier.newLayout = newL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage{}, dstStage{};

        if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else {
            throw std::runtime_error("unsupported layout transition");
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        m_device.endSingleTimeCommands(cmd); // 你工程中已有封装可直接用 :contentReference[oaicite:3]{index=3}
    }

    void LveTexture::Upload(const void* pixels, VkDeviceSize size, uint32_t w, uint32_t h) 
    {
        VkBuffer staging{};
        VkDeviceMemory stagingMem{};

        // 用你已有的封装创建 staging buffer 并映射写入
        m_device.createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging,
            stagingMem);

        void* data;
        vkMapMemory(m_device.device(), stagingMem, 0, size, 0, &data);
        std::memcpy(data, pixels, static_cast<size_t>(size));
        vkUnmapMemory(m_device.device(), stagingMem);

        // 拷贝到图像并清理 staging
        m_device.copyBufferToImage(staging, m_image, w, h, 1); // 已有封装 :contentReference[oaicite:4]{index=4}
        vkDestroyBuffer(m_device.device(), staging, nullptr);
        vkFreeMemory(m_device.device(), stagingMem, nullptr);
    }

    LveTexture::~LveTexture() {
        if (m_ownSampler && m_sampler)   vkDestroySampler(m_device.device(), m_sampler, nullptr);
        if (m_imageView) vkDestroyImageView(m_device.device(), m_imageView, nullptr);
        if (m_image)     vkDestroyImage(m_device.device(), m_image, nullptr);
        if (m_memory)    vkFreeMemory(m_device.device(), m_memory, nullptr);
    }

}