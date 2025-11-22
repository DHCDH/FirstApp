#include "LveModel.h"

#include "LveUtils.h"
#include "LveFrameInfo.h"

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
	m_submeshes = builder.submeshes;
}

LveModel::~LveModel()
{
	
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
	uint32_t vertexSize = sizeof(vertices[0]);

	LveBuffer stagingBuffer(m_lveDevice, 
		vertexSize, 
		m_vertexCount, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.Map();
    stagingBuffer.WriteToBuffer((void*)vertices.data());

	/*初始化顶点缓冲区*/
	m_vertexBuffer = std::make_unique<LveBuffer>(m_lveDevice,
		vertexSize,
		m_vertexCount,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	m_lveDevice.copyBuffer(stagingBuffer.GetBuffer(), m_vertexBuffer->GetBuffer(), bufferSize);
}

void LveModel::CreateIndexBuffer(const std::vector<uint32_t>& indices)
{
	m_indexCount = static_cast<uint32_t>(indices.size());
	m_hasIndexBuffer = m_indexCount > 0;

	if (!m_hasIndexBuffer) {
		return;
	}

	VkDeviceSize bufferSize = sizeof(indices[0]) * m_indexCount;
	uint32_t indexSize = sizeof(indices[0]);

	LveBuffer stagingBuffer(m_lveDevice,
		indexSize,
		m_indexCount,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	stagingBuffer.Map();
	stagingBuffer.WriteToBuffer((void*)indices.data());

	m_indexBuffer = std::make_unique<LveBuffer>(m_lveDevice,
		indexSize,
		m_indexCount,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	m_lveDevice.copyBuffer(stagingBuffer.GetBuffer(), m_indexBuffer->GetBuffer(), bufferSize);
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
	VkBuffer buffer[] = { m_vertexBuffer->GetBuffer()};
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffer, offsets);

	if (m_hasIndexBuffer) {
		vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
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

	/*materials和shapes的size不一定相同*/
	std::cout << filepath << ":\nshapes.size = " << shapes.size() << " materials.size = " << materials.size() << "\n";

	vertices.clear();
	indices.clear();

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};
	for (const auto& shape : shapes) {
		/*记录shape再全局索引数组中的起点*/
		uint32_t firstIndexThisShape = static_cast<uint32_t>(indices.size());

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

		//以整个 shape 作为一个 submesh（一般对应一个 usemtl）
		uint32_t indexCountThisShape = static_cast<uint32_t>(indices.size()) - firstIndexThisShape;
		int materialId = -1;
		if (!shape.mesh.material_ids.empty()) {
			// 若该 shape 由单一 usemtl 生成，取第 1 个即可；否则后续可按 material_ids 拆更细
			materialId = shape.mesh.material_ids[0];
		}
		lve::LveModel::Submesh sm;
		sm.firstIndex = firstIndexThisShape;
		sm.indexCount = indexCountThisShape;
		sm.materialId = materialId;
		submeshes.push_back(sm);
	}
}

void LveModel::DrawSubmesh(VkCommandBuffer cmd, uint32_t i) const {
	if (!m_hasIndexBuffer) return;
	const auto& s = m_submeshes[i];
	vkCmdDrawIndexed(cmd, s.indexCount, 1, s.firstIndex, 0, 0);
}

void LveModel::DrawInstanced(VkCommandBuffer commandBuffer, uint32_t instanceCount, uint32_t firstInstance) const
{
	if (instanceCount == 0) return;

	if (m_hasIndexBuffer) {
		vkCmdDrawIndexed(commandBuffer,
			m_indexCount,
			instanceCount,
			0,
			0,
			firstInstance);
	}
	else {
		vkCmdDraw(commandBuffer,
			m_vertexCount,
			instanceCount,
			0,
			firstInstance);
	}
}
void LveModel::DrawSubmeshInstanced(VkCommandBuffer commandBuffer, uint32_t submeshIndex,
	uint32_t instanceCount, uint32_t firstInstance) const
{
	if (!m_hasIndexBuffer || instanceCount == 0) return;
	if (submeshIndex >= m_submeshes.size()) return;

	const auto& s = m_submeshes[submeshIndex];

	vkCmdDrawIndexed(
		commandBuffer,
		s.indexCount, 
		instanceCount,
		s.firstIndex, 
		0,            
		firstInstance);
}

}