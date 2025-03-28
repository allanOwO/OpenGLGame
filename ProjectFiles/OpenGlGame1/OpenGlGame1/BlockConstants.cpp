#include "BlockConstants.h"

const glm::vec3 unitCubeVertices[8] = {
    {0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 1.0f}
};

const float texCoords[4][2] = {
    {0.0f, 0.0f}, {1.0f, 0.0f},
    {1.0f, 1.0f}, {0.0f, 1.0f}
};

const glm::vec3 normals[6] = {
    {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
    {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}
};

const int faces[6][4] = {
    {3, 2, 1, 0}, // Front
    {6, 7, 4, 5}, // Back
    {7, 3, 0, 4}, // Left
    {2, 6, 5, 1}, // Right
    {7, 6, 2, 3}, // Top
    {0, 1, 5, 4}  // Bottom
};