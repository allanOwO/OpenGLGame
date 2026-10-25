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
//
//LOD meshes use a shrunk copy (see Chunk::downsample): same layout, but size is 8, 4 or 2 and each cell stands for 2, 4 or 8 blocks
struct ChunkMeshInput {
	static constexpr int paddedSize = 18;//full detail section size + 1 each side
	int size = 16;//cells along each side, not counting the border
	std::vector<BlockType> blocks;//(size + 2)^3, x/y/z all run from -1 to size

	int padded() const { return size + 2; }

	//section local, so -1 and size are the border
	BlockType get(int x, int y, int z) const {
		const int p = padded();
		return blocks[(static_cast<size_t>(y + 1) * p + (z + 1)) * p + (x + 1)];
	}
};
