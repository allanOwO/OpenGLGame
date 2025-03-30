#include "BlockConstants.h"
#include <unordered_map>
#include "BlockType.h"
#include <iostream>

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


// Order: bottom-left, bottom-right, top-right, top-left
const float texCoords[4][2] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 0.0f, 1.0f }
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

const BlockFace faceEnum[6] = {
        BlockFace::FRONT, BlockFace::BACK, BlockFace::LEFT,
        BlockFace::RIGHT, BlockFace::TOP, BlockFace::BOTTOM
};

const BlockUV getBlockUV(BlockType blockType, BlockFace face) {
    int blockIndex = 0; // Default index for the texture atlas

    switch (blockType) {
    case BlockType::DIRT:
        blockIndex = 0; // Uses the same texture for all faces
        break;
    case BlockType::GRASS:
        if (face == BlockFace::TOP)
            blockIndex = 2; // Grass Top
        else if (face == BlockFace::BOTTOM)
            blockIndex = 0; // Dirt for bottom
        else
            blockIndex = 1; // Grass Side
        break;

    case BlockType::STONE:
        blockIndex = 3;
        break;
    }

    // Compute UVs based on atlas index
    float col = blockIndex % 4;
    float row =3- blockIndex / 4;
    float uvSize = 0.25f;

    return {
        { col * uvSize, row * uvSize },
        { (col + 1) * uvSize, (row + 1) * uvSize }
    };
}
