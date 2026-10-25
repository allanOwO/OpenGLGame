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

// Meshes keep their faces grouped by the way they point, so whole groups facing away from the camera can be skipped
// (the GPU would throw those triangles away anyway, but only after running the vertex shader on them)
// Up and down share a group: tops are nearly always seen, and terrain has almost no bottoms
// The order puts the three groups seen from most directions next to each other, so they're usually one draw
enum FaceGroup { NegXFaces, NegZFaces, VerticalFaces, PosXFaces, PosZFaces, faceGroupCount };

// face index as in BlockFace: 0 Front (-Z), 1 Back (+Z), 2 Left (-X), 3 Right (+X), 4 Top, 5 Bottom
inline int faceGroupOf(int face) {
    static const int groups[6] = { NegZFaces, PosZFaces, NegXFaces, PosXFaces, VerticalFaces, VerticalFaces };
    return groups[face];
}

static_assert(sizeof(PackedVertex) == 16, "PackedVertex layout changed, update ChunkMeshPool::setVertexAttributes and vertex_shader.glsl");
