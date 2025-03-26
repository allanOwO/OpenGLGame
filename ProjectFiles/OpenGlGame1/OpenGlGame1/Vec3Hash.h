#pragma once
#include <glm/glm.hpp>
#include<functional>

struct Vec3Hash {//key for map
	size_t operator()(const glm::vec3& v) const {
		return std::hash<float>()(v.x) ^ std::hash<float>()(v.y) ^ std::hash<float>()(v.z);
	}
};
