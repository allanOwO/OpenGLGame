#pragma once
#include <vector>
#include "BlockType.h"
#include "VertexPacking.h"


struct MeshData {
	//one section in one range of ChunkMeshPool's buffer, all block types share the texture atlas
	//4 vertices per quad, no index list: every quad uses the pool's shared 0,1,2 0,2,3 indices
	std::vector<PackedVertex> vertices;
};

//copy of one 16x16x16 section plus a 1 block border from around it (neighbour columns, and the sections above and below)
//mesh threads only read this, so the real chunks can be edited or unloaded while meshing
struct ChunkMeshInput {
	static constexpr int paddedSize = 18;//section size + 1 each side
	std::vector<BlockType> blocks;//paddedSize^3, x/y/z all run from -1 to 16

	//section local, so -1 and 16 are the border
	BlockType get(int x, int y, int z) const {
		return blocks[(static_cast<size_t>(y + 1) * paddedSize + (z + 1)) * paddedSize + (x + 1)];
	}
};
