#pragma once
#include <vector>
#include "BlockType.h"
#include "VertexPacking.h"


struct MeshData {
	//whole chunk in one range of ChunkMeshPool's buffer, all block types share the texture atlas
	//4 vertices per quad, no index list: every quad uses the pool's shared 0,1,2 0,2,3 indices
	std::vector<PackedVertex> vertices;
	int tallestBlock = 0;//highest non air block, for frustum culling
};

//copy of a chunk's blocks plus a 1 block border from its neighbours
//mesh threads only read this, so the real chunks can be edited or unloaded while meshing
struct ChunkMeshInput {
	static constexpr int paddedSize = 18;//chunkSize + 1 each side
	std::vector<BlockType> blocks;//paddedSize * paddedSize * height, x/z run from -1 to chunkSize
	int maxY = -1;//highest non air block in the chunk itself, -1 if empty

	//x and z are chunk local, so -1 and chunkSize are the neighbour border
	BlockType get(int x, int y, int z) const {
		return blocks[(static_cast<size_t>(y) * paddedSize + (z + 1)) * paddedSize + (x + 1)];
	}
};
