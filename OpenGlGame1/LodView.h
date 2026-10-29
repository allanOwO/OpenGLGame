#pragma once
#include <glm/glm.hpp>

#include "Frustum.h"

// What World and FarTerrain need from the camera to pick each column's level of detail
// Zooming makes far things look close, so inside the zoomed view they get the detail of something zoom times nearer
struct LodView {
	float zoom = 1.0f;//1 = not zoomed
	glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);//camera direction, only looked at while zoomed
	Frustum zoomedView;//what the zoomed camera sees, only set while zoomed
	Frustum keepView;//a wider view around it, columns inside keep their zoom detail (see World::checkLod)
};
