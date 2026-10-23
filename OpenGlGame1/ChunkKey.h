#pragma once
#include <cstdint>

// Packs chunk coordinates into one 64 bit number so chunks can be stored in an unordered_map
// x goes in the top 32 bits, z in the bottom 32, so every chunk position gets a unique key
inline uint64_t getChunkKey(int x, int z) {
	uint32_t ux = static_cast<uint32_t>(x);
	uint32_t uz = static_cast<uint32_t>(z);
	return (static_cast<uint64_t>(ux) << 32) | uz;
}

//reverse of getChunkKey
inline int chunkKeyX(uint64_t key) { return static_cast<int32_t>(key >> 32); }
inline int chunkKeyZ(uint64_t key) { return static_cast<int32_t>(key & 0xFFFFFFFF); }
