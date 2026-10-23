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
    uint16_t padding;   // keeps the size a round 16 bytes (2 bytes)
};

static_assert(sizeof(PackedVertex) == 16, "PackedVertex layout changed, update Chunk::initializeBuffers and vertex_shader.glsl");
