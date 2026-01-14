#include "Frustum.h"


void Frustum::update(const glm::mat4& viewProj) {

    // Extract frustum planes from the view-projection matrix
    // Using the method from: https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
    planes[0] = glm::vec4( // Left
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    planes[1] = glm::vec4( // Right
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    planes[2] = glm::vec4( // Bottom
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    planes[3] = glm::vec4( // Top
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    planes[4] = glm::vec4( // Near
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );
    planes[5] = glm::vec4( // Far
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
     
}

bool Frustum::isBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
    for (int i = 0; i < 6; i++) {
        const glm::vec4& plane = planes[i];
        glm::vec3 positive = min;
        if (plane.x > 0) positive.x = max.x;
        if (plane.y > 0) positive.y = max.y;
        if (plane.z > 0) positive.z = max.z;

        float distance = glm::dot(glm::vec3(plane), positive) + plane.w;
        if (distance < 0) {
            return false; // Box is outside this plane
        }
    }
    return true; // Box is inside or intersects the frustum
}