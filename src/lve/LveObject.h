#pragma once

#include "LveModel.h"

#include <gtc/matrix_transform.hpp>

#include <memory>
#include <unordered_map>

namespace lve {

struct TransformComponent {
	glm::vec3 translation{};	// 用于上下左右移动对象
	glm::vec3 scale{ 1.f, 1.f, 1.f };
	glm::vec3 rotation{};

    // Matrix corrsponds to Translate * Ry * Rx * Rz * Scale
    // Rotations correspond to Tait-bryan angles of Y(1), X(2), Z(3)
    // https://en.wikipedia.org/wiki/Euler_angles#Rotation_matrix
	glm::mat4 mat4();
	glm::mat3 normalMatrix();
};

class LveObject {
public:
	using id_t = unsigned int;
	using Map = std::unordered_map<id_t, LveObject>;

	static LveObject CreateObject() {
		static id_t current_id = 0;
		return LveObject{ current_id++ };
	}

	LveObject(const LveObject&) = delete;
	LveObject& operator=(const LveObject&) = delete;
	LveObject(LveObject&&) = default;
	LveObject& operator=(LveObject&&) = default;

	id_t getId() const { return id; }

    std::shared_ptr<LveModel> model{};
	glm::vec3 color{1.f, 1.f, 1.f};
	TransformComponent transform{};

private:
	LveObject(id_t obj_id) : id{ obj_id } {}

	id_t id;
};

}