#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// Writes to path.tmp then renames it over path
// If the game crashes or the disk fills mid save, the old file is still there instead of a half written one
inline bool writeFileAtomically(const std::string& path, const std::vector<uint8_t>& data) {
	std::string tempPath = path + ".tmp";
	{
		std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!file) return false;
	}// closing the file flushes it before the rename

	std::error_code error;
	std::filesystem::rename(tempPath, path, error);//replaces the old file in one step
	return !error;
}

inline bool readWholeFile(const std::string& path, std::vector<uint8_t>& data) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return true;
}
