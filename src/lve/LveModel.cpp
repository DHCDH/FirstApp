#include "LveModel.h"

#include "LveUtils.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/hash.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace std {
	template<>
	struct hash<lve::LveModel::Vertex> {
		size_t operator()(lve::LveModel::Vertex const& vertex) const {
			size_t seed = 0;
			lve::HashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
			return seed;
		}
	};
}

namespace lve { 

LveModel::LveModel(LveDevice& m_lveDevice, const LveModel::Builder& builder)
	: m_lveDevice{ m_lveDevice }
{
	CreateVertexBuffer(builder.vertices);
	CreateIndexBuffer(builder.indices);
}

LveModel::~LveModel()
{
	vkDestroyBuffer(m_lveDevice.device(), m_vertexBuffer, nullptr);
	vkFreeMemory(m_lveDevice.device(), m_vertexBufferMemory, nullptr);

	if (m_hasIndexBuffer) {
		vkDestroyBuffer(m_lveDevice.device(), m_indexBuffer, nullptr);
		vkFreeMemory(m_lveDevice.device(), m_indexBufferMemory, nullptr);
	}
}

std::unique_ptr<LveModel> LveModel::CreateModelFromFile(LveDevice& m_lveDevice, const std::string& filepath)
{
	Builder builder;
	builder.LoadModel(filepath);

	std::cout << "Vertex count: " << builder.vertices.size() << "\n";

	return std::make_unique<LveModel>(m_lveDevice, builder);
}

void LveModel::CreateVertexBuffer(const std::vector<Vertex>& vertices)
{
	m_vertexCount = static_cast<uint32_t>(vertices.size());
	assert(m_vertexCount >= 3 && "Vertex count must be at least 3");
    VkDeviceSize bufferSize = sizeof(vertices[0]) * m_vertexCount;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

	m_lveDevice.createBuffer(bufferSize,
						   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,	// 告诉Vulkan正在创建的缓冲区将仅用作内存传输操作的源位置
						   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,	// HOST: CPU
						   stagingBuffer,
						   stagingBufferMemory);
    
	void* data;
	vkMapMemory(m_lveDevice.device(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_lveDevice.device(), stagingBufferMemory);

	m_lveDevice.createBuffer(
		bufferSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_vertexBuffer,
		m_vertexBufferMemory);

	m_lveDevice.copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

	vkDestroyBuffer(m_lveDevice.device(), stagingBuffer, nullptr);
	vkFreeMemory(m_lveDevice.device(), stagingBufferMemory, nullptr);
}

void LveModel::CreateIndexBuffer(const std::vector<uint32_t>& indices)
{
	m_indexCount = static_cast<uint32_t>(indices.size());
	m_hasIndexBuffer = m_indexCount > 0;

	if (!m_hasIndexBuffer) {
		return;
	}

	VkDeviceSize bufferSize = sizeof(indices[0]) * m_indexCount;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

	m_lveDevice.createBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,	// 告诉Vulkan正在创建的缓冲区将仅用作内存传输操作的源位置
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,	// HOST: CPU
		stagingBuffer,
		stagingBufferMemory);

	void* data;
	vkMapMemory(m_lveDevice.device(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_lveDevice.device(), stagingBufferMemory);

	m_lveDevice.createBuffer(
		bufferSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_indexBuffer,
		m_indexBufferMemory);

	m_lveDevice.copyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

	vkDestroyBuffer(m_lveDevice.device(), stagingBuffer, nullptr);
	vkFreeMemory(m_lveDevice.device(), stagingBufferMemory, nullptr);
}

void LveModel::Draw(VkCommandBuffer commandBuffer) 
{
	/*检查是否存在索引缓冲区*/
	if (m_hasIndexBuffer) {
		vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
	}
	else {
		vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
	}
	
}

void LveModel::Bind(VkCommandBuffer commandBuffer) {
	VkBuffer buffer[] = { m_vertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffer, offsets);

	if (m_hasIndexBuffer) {
		vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
	}
}

std::vector<VkVertexInputBindingDescription> LveModel::Vertex::GetBindingDescriptions()
{
	std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
	bindingDescriptions[0].binding = 0;
	bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> LveModel::Vertex::GetAttributeDescriptions()
{
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

	attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
	attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) });
	attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
	attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });


	return attributeDescriptions;
}

void LveModel::Builder::LoadModel(const std::string& filepath)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
		std::cerr << "filepath: " << filepath << "\n";
		std::cerr << "warn: " << warn << "\n";
		std::cerr << "err: " << err << "\n";
        throw std::runtime_error(warn + err);
	}

	vertices.clear();
	indices.clear();

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex{};

			if (index.vertex_index >= 0) {
				vertex.position = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2],
				};

				vertex.color = {
					attrib.colors[3 * index.vertex_index + 0],
					attrib.colors[3 * index.vertex_index + 1],
					attrib.colors[3 * index.vertex_index + 2],
				};
			}

			if (index.normal_index >= 0) {
				vertex.normal = {
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2],
				};
			}

			if (index.texcoord_index >= 0) {
				vertex.uv = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					attrib.texcoords[2 * index.texcoord_index + 1],
				};
			}

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}
			indices.push_back(uniqueVertices[vertex]);

		}
	}
}

}