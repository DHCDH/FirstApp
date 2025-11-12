#pragma once

#include "LveDevice.h"
#include <string>

namespace lve {

class LveTexture {
public:
	LveTexture(LveDevice& device, const std::string& filepath, bool srgb = true);
	LveTexture(LveDevice& device, const std::string& filepath, VkSampler sharedSmapler, bool srgb = true);
    ~LveTexture();

	LveTexture(const LveTexture&) = delete;
	LveTexture& operator=(const LveTexture&) = delete;
    LveTexture(LveTexture&&) = delete;
    LveTexture& operator=(LveTexture&&) = delete;

	VkImageView   GetImageView() const { return m_imageView; }
	VkSampler     GetSampler()   const { return m_sampler; }
	VkImage       GetImage()     const { return m_image; }
	VkFormat      GetFormat()    const { return m_format; }

	VkDescriptorImageInfo DescriptorInfo() const;

private:
	void CreateFromFile(const std::string& filepath, bool srgb);
	void CreateImage(uint32_t w, uint32_t h, VkFormat format);
	void CreateImageView();
	void CreateSampler();

	void TransitionLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
	void Upload(const void* pixels, VkDeviceSize size, uint32_t w, uint32_t h);
	
private:
	LveDevice& m_device;
	VkImage m_image{ VK_NULL_HANDLE };
	VkDeviceMemory m_memory{ VK_NULL_HANDLE };
	VkImageView m_imageView{ VK_NULL_HANDLE };
	VkSampler m_sampler{ VK_NULL_HANDLE };
	VkFormat m_format{ VK_FORMAT_UNDEFINED };
	uint32_t m_width{ 0 };
	uint32_t m_height{ 0 };
	bool m_ownSampler{ true };	// 是否由本对象自己创建并负责销毁
};

}