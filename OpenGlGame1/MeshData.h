#pragma once
#include <map>
#include <vector>
#include "BlockType.h"
#include "VertexPacking.h"


struct MeshData {
	//by type so can apply textures in batches
	std::map<BlockType, std::vector<PackedVertex>> packedVerticesByType;
	std::map<BlockType, std::vector<unsigned int>> indicesByType;
	std::map<BlockType, unsigned int> baseIndicesByType;
};