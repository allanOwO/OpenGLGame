#pragma once
#include <map>
#include <vector>
#include "BlockType.h"

struct MeshData {
	//by type so can apply textures in batches
	std::map<BlockType, std::vector<float>> verticesByType;
	std::map<BlockType, std::vector<unsigned int>> indicesByType;
	std::map<BlockType, unsigned int> baseIndicesByType;
};