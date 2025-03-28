#ifndef BLOCK_CONSTANTS_H
#define BLOCK_CONSTANTS_H

#include <glm/glm.hpp> 

// Precomputed vertices for a unit cube (from (0,0,0) to (1,1,1))
extern const glm::vec3 unitCubeVertices[8];

// Texture coordinates for a face (4 vertices)
extern const float texCoords[4][2];

// Normals for each face of the cube (6 faces)
extern const glm::vec3 normals[6];

// Face definitions: for each of the 6 faces, indices into the unitCubeVertices array (4 vertices per face)
extern const int faces[6][4];

#endif // BLOCK_CONSTANTS_H