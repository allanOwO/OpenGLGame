#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include "BlockType.h"

// Encodes a chunk's blocks as runs of (block type, count), most chunks are long runs so this is tiny
// Byte layout (little endian), this is also what will go in save files:
//   [uint8 format version]
//   repeated: [uint8 block type][uint16 run length]
std::vector<uint8_t> encodeBlocks(const std::vector<BlockType>& blocks);

// Reverse of encodeBlocks, returns false if the data is corrupt or the wrong size
bool decodeBlocks(const std::vector<uint8_t>& data, size_t expectedBlockCount, std::vector<BlockType>& outBlocks);

// Holds encoded chunks the player has modified, keyed by chunk key (see ChunkKey.h)
// Unmodified chunks are never stored, they can be regenerated from the seed
// For now this lives in memory, saving a world will write these out to disk
class ChunkStorage
{
public:
	void store(uint64_t key, std::vector<uint8_t> data);

	// Moves the data out if it exists, the loaded chunk is the up to date copy from then on
	bool take(uint64_t key, std::vector<uint8_t>& outData);

	size_t size() const { return chunks.size(); }
	template <typename Visit>
	void forEach(Visit visit) const { for (const auto& pair : chunks) visit(pair.first, pair.second); }

	// Chunk file layout (little endian):
	//   "VXCK" magic, uint32 file version, uint32 chunk count
	//   per chunk: int32 chunk x, int32 chunk z, uint32 byte count, encoded blocks (see encodeBlocks)
	bool saveToFile(const std::string& path) const;
	bool loadFromFile(const std::string& path);//false if missing or corrupt, storage is left unchanged then

private:
	std::unordered_map<uint64_t, std::vector<uint8_t>> chunks;
};
