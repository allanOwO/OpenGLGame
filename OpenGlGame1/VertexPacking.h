#pragma once
#include <cstdint>

// One corner of a chunk face, 16 bytes
// Greedy meshing makes quads several blocks wide, so texture coordinates are in blocks (not 0-1)
// and the shader repeats the atlas tile once per block using fract()
struct PackedVertex {
    int16_t pos[3];     // chunk local position (6 bytes)
    uint8_t normal;     // face index 0-5, same order as BlockFace (1 byte)
    uint8_t ao;         // ambient occlusion, 0 = darkest corner, 3 = nothing nearby (1 byte)
    uint16_t uv[2];     // texture coordinates in blocks, the tile repeats every 1.0 (4 bytes)
    uint16_t tile;      // which tile in the texture atlas (2 bytes)
    uint16_t place;     // which section of its region this vertex belongs to, see packPlace (2 bytes)
};

// Every vertex is positioned relative to its 8x8 column region instead of its own section,
// so the meshes of a whole region can be copied end to end into one mesh and drawn with one origin
// The position stays section local (small numbers), and the shader adds place * 16 on top:
// bits 0-2 column x in the region, bits 3-5 column z, bits 6-9 section (height)
inline uint16_t packPlace(int columnX, int columnZ, int section) {
    return static_cast<uint16_t>(columnX | (columnZ << 3) | (section << 6));
}

static_assert(sizeof(PackedVertex) == 16, "PackedVertex layout changed, update ChunkMeshPool::setVertexAttributes and vertex_shader.glsl");
