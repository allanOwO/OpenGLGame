#pragma once
#include <glm/glm.hpp>
#include<functional>
//this is like a dictionary in c#

struct Vec3Hash {//key for map
	size_t operator()(const glm::vec3& v) const {
		return std::hash<float>()(v.x) ^ std::hash<float>()(v.y) ^ std::hash<float>()(v.z);
	}
};

struct IVec3Hash {//blocks
	std::size_t operator()(const glm::ivec3& v) const {
		return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
	}
};
