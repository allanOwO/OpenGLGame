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


struct IVec2Hash {
	size_t operator()(const glm::ivec2& v) const {
		return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1);
	}
};

struct PairHash {
	template <typename T1, typename T2>
	std::size_t operator ()(const std::pair<T1, T2>& p) const {
		auto h1 = std::hash<T1>{}(p.first);
		auto h2 = std::hash<T2>{}(p.second);

		// Combine the two hash values into one
		return h1 ^ (h2 << 1); // or use some other method to combine the hashes
	}
};
