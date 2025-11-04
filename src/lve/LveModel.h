#pragma

#include "LveDevice.h"
#include "LveBuffer.h"

#define GLM_FORCE_RADIANS	// 无论在什么系统上，glm都会希望角度以弧度指定
#define GLM_FORCE_DEPTH_ZERO_TO_ONE	//深度缓冲区值范围从0到1，而不是-1到1（OpenGL）
#include <glm.hpp>

#include <vector>
#include <memory>

namespace lve { 

/*在CPU创建顶点数据，分配内存并将数据复制到GPU*/
class LveModel {

public:

	struct Vertex {
		glm::vec3 position{};
		glm::vec3 color{};
		glm::vec3 normal{};
		glm::vec2 uv{};	// 二维纹理坐标

		static std::vector<VkVertexInputBindingDescription> GetBindingDescriptions();
		static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();

		bool operator==(const Vertex& other) const {
			return position == other.position && color == other.color && normal == other.normal && uv == other.uv;
		}

        Vertex(const glm::vec3& pos) : position(pos) {}
		Vertex(const glm::vec3& pos, const glm::vec3& col) : position(pos), color(col) {}
        Vertex() = default;
	};

	struct Builder {
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};

		void LoadModel(const std::string& filepath);
	};

	LveModel(LveDevice& lveDevice, const Builder& builder);
	~LveModel();

	static std::unique_ptr<LveModel> CreateModelFromFile(LveDevice& lveDevice, const std::string& filepath);

	LveModel(const LveModel&) = delete;
	LveModel& operator = (const LveModel&) = delete;

	void Bind(VkCommandBuffer commandBuffer);
	void Draw(VkCommandBuffer commandBuffer);

private:
	void CreateVertexBuffer(const std::vector<Vertex>& vertices);
	void CreateIndexBuffer(const std::vector<uint32_t>& indices);

	LveDevice& m_lveDevice;

	/*顶点缓冲区*/
	std::unique_ptr<LveBuffer> m_vertexBuffer;
	uint32_t m_vertexCount;

	bool m_hasIndexBuffer = false;

	/*索引缓冲区*/
	std::unique_ptr<LveBuffer> m_indexBuffer;
	uint32_t m_indexCount;
};

}