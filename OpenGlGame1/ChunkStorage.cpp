#include "ChunkStorage.h"

// Bump this if the byte layout changes, so old saves can be detected
static const uint8_t blockFormatVersion = 1;

static const size_t maxRunLength = 0xFFFF;//largest count a uint16 can hold

std::vector<uint8_t> encodeBlocks(const std::vector<BlockType>& blocks) {
	std::vector<uint8_t> data;
	data.push_back(blockFormatVersion);

	size_t i = 0;
	while (i < blocks.size()) {
		// Count how many of the same block follow
		BlockType type = blocks[i];
		size_t run = 1;
		while (i + run < blocks.size() && blocks[i + run] == type && run < maxRunLength) {
			run++;
		}

		// Write the bytes one at a time so the file is the same on any machine (little endian)
		data.push_back(static_cast<uint8_t>(type));
		data.push_back(static_cast<uint8_t>(run & 0xFF));
		data.push_back(static_cast<uint8_t>(run >> 8));
		i += run;
	}
	return data;
}

bool decodeBlocks(const std::vector<uint8_t>& data, size_t expectedBlockCount, std::vector<BlockType>& outBlocks) {
	if (data.empty() || data[0] != blockFormatVersion) return false;
	if ((data.size() - 1) % 3 != 0) return false;//every run is 3 bytes

	outBlocks.clear();
	outBlocks.reserve(expectedBlockCount);

	for (size_t i = 1; i < data.size(); i += 3) {
		uint8_t type = data[i];
		size_t run = data[i + 1] | (static_cast<size_t>(data[i + 2]) << 8);
		if (type > static_cast<uint8_t>(BlockType::WATER)) return false;//unknown block type
		if (outBlocks.size() + run > expectedBlockCount) return false;//too many blocks

		outBlocks.insert(outBlocks.end(), run, static_cast<BlockType>(type));
	}

	return outBlocks.size() == expectedBlockCount;
}

void ChunkStorage::store(uint64_t key, std::vector<uint8_t> data) {
	chunks[key] = std::move(data);
}

bool ChunkStorage::take(uint64_t key, std::vector<uint8_t>& outData) {
	auto it = chunks.find(key);
	if (it == chunks.end()) return false;

	outData = std::move(it->second);
	chunks.erase(it);
	return true;
}
