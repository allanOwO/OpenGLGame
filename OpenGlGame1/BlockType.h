#pragma once
#include <cstdint>

//1 byte per block, a chunk is 16*512*16 blocks so this keeps it at 128KB
enum class BlockType : uint8_t {
	AIR,
	DIRT,
	STONE,
	GRASS,
	WATER,
	SNOW,
};
