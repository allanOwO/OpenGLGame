#include "BlockConstants.h"

int getBlockTile(BlockType blockType, BlockFace face) {
    switch (blockType) {
    case BlockType::DIRT:
        return 0; // Uses the same texture for all faces
    case BlockType::GRASS:
        if (face == BlockFace::TOP)
            return 2; // Grass Top
        else if (face == BlockFace::BOTTOM)
            return 0; // Dirt for bottom
        else
            return 1; // Grass Side
    case BlockType::STONE:
        return 3;
    case BlockType::WATER:
        return 4;
    case BlockType::SNOW:
        return 5;
    default:
        return 0;
    }
}
