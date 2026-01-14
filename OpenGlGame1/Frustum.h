#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Frustum
{
public:
	glm::vec4 planes[6];
	void update(const glm::mat4& viewProj);
	bool isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const;
};

