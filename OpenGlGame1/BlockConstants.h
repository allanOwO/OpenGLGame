#ifndef BLOCK_CONSTANTS_H
#define BLOCK_CONSTANTS_H

#include "BlockType.h"

// Same order as the face index used in meshes and the shader (Front = -Z, Back = +Z, Left = -X, Right = +X)
enum class BlockFace { FRONT, BACK, LEFT, RIGHT, TOP, BOTTOM };

// The texture atlas is atlasTiles x atlasTiles tiles, must match atlasTiles in vertex_shader.glsl
constexpr int atlasTiles = 4;

// Which atlas tile a block face uses (counted left to right, top to bottom)
int getBlockTile(BlockType blockType, BlockFace face);

#endif // BLOCK_CONSTANTS_H
