#include "ChunkStorage.h"
#include <algorithm>
#include "ChunkKey.h"
#include "FileUtil.h"

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

// ---------- chunk file ----------

static const char chunkFileMagic[4] = { 'V', 'X', 'C', 'K' };//identifies the file type
static const uint32_t chunkFileVersion = 1;

static void writeU32(std::vector<uint8_t>& out, uint32_t value) {
	for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Reads from data at offset, moving offset forward; false if there aren't enough bytes left
static bool readU32(const std::vector<uint8_t>& data, size_t& offset, uint32_t& value) {
	if (data.size() - offset < 4) return false;
	value = 0;
	for (int i = 0; i < 4; i++) value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
	offset += 4;
	return true;
}

bool ChunkStorage::saveToFile(const std::string& path) const {
	std::vector<uint8_t> out(chunkFileMagic, chunkFileMagic + 4);
	writeU32(out, chunkFileVersion);
	writeU32(out, static_cast<uint32_t>(chunks.size()));

	for (const auto& pair : chunks) {
		writeU32(out, static_cast<uint32_t>(chunkKeyX(pair.first)));//int32 stored as its bit pattern
		writeU32(out, static_cast<uint32_t>(chunkKeyZ(pair.first)));
		writeU32(out, static_cast<uint32_t>(pair.second.size()));
		out.insert(out.end(), pair.second.begin(), pair.second.end());
	}
	return writeFileAtomically(path, out);
}

bool ChunkStorage::loadFromFile(const std::string& path) {
	std::vector<uint8_t> data;
	if (!readWholeFile(path, data)) return false;
	if (data.size() < 12 || !std::equal(chunkFileMagic, chunkFileMagic + 4, data.begin())) return false;

	size_t offset = 4;
	uint32_t version = 0, count = 0;
	if (!readU32(data, offset, version) || version != chunkFileVersion) return false;
	if (!readU32(data, offset, count)) return false;

	// Read into a separate map so a corrupt file can't leave storage half loaded
	std::unordered_map<uint64_t, std::vector<uint8_t>> loaded;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t x = 0, z = 0, size = 0;
		if (!readU32(data, offset, x) || !readU32(data, offset, z) || !readU32(data, offset, size)) return false;
		if (data.size() - offset < size) return false;//file cut short

		auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);
		loaded[getChunkKey(static_cast<int32_t>(x), static_cast<int32_t>(z))] = std::vector<uint8_t>(begin, begin + size);
		offset += size;
	}

	chunks = std::move(loaded);
	return true;
}
