#ifndef BLOCK_CONSTANTS_H
#define BLOCK_CONSTANTS_H

#include <glm/glm.hpp> 
#include "BlockType.h"

struct BlockUV {
    glm::vec2 UvBl;//bottom left
    glm::vec2 UvTr;//top right
}; 

enum class BlockFace { TOP, FRONT,BACK,LEFT,RIGHT, BOTTOM };


// Precomputed vertices for a unit cube (from (0,0,0) to (1,1,1))
extern const glm::vec3 unitCubeVertices[8];

extern const float texCoords[4][2];

// Normals for each face of the cube (6 faces)
extern const glm::vec3 normals[6];

// Face definitions: for each of the 6 faces, indices into the unitCubeVertices array (4 vertices per face)
extern const int faces[6][4];

extern const BlockFace faceEnum[6];

extern const BlockUV getBlockUV(BlockType block, BlockFace face);

#endif // BLOCK_CONSTANTS_H