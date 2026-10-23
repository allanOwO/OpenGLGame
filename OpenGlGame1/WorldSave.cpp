#include "WorldSave.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "FileUtil.h"

namespace fs = std::filesystem;

static const char* savesFolder = "saves";
static const int infoFileVersion = 1;

static std::string infoPath(const std::string& name) {
	return WorldSave::worldFolder(name) + "/world.txt";
}

bool WorldSave::isNameCharacter(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_';
}

bool WorldSave::isValidName(const std::string& name) {
	if (name.empty() || name.size() > maxNameLength) return false;
	// Windows doesn't allow folder names ending in a space, and leading spaces just look like a bug
	if (name.front() == ' ' || name.back() == ' ') return false;
	return std::all_of(name.begin(), name.end(), isNameCharacter);
}

bool WorldSave::exists(const std::string& name) {
	std::error_code error;
	return fs::exists(worldFolder(name), error);
}

std::string WorldSave::worldFolder(const std::string& name) {
	return std::string(savesFolder) + "/" + name;
}

std::string WorldSave::chunksPath(const std::string& name) {
	return worldFolder(name) + "/chunks.dat";
}

std::vector<std::string> WorldSave::listWorlds() {
	std::vector<std::pair<fs::file_time_type, std::string>> found;
	std::error_code error;
	if (!fs::exists(savesFolder, error)) return {};

	for (const auto& entry : fs::directory_iterator(savesFolder, error)) {
		if (!entry.is_directory()) continue;
		fs::path info = entry.path() / "world.txt";
		if (!fs::exists(info, error)) continue;//not a world folder
		found.emplace_back(fs::last_write_time(info, error), entry.path().filename().string());
	}

	// world.txt is rewritten on every save, so its time is when the world was last played
	std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	std::vector<std::string> names;
	for (const auto& pair : found) names.push_back(pair.second);
	return names;
}

bool WorldSave::writeInfo(const WorldInfo& info) {
	std::error_code error;
	fs::create_directories(worldFolder(info.name), error);
	if (error) return false;

	std::ostringstream text;
	text << "version=" << infoFileVersion << "\n";
	text << "name=" << info.name << "\n";
	text << "seed=" << info.seed << "\n";
	if (info.hasPlayer) {
		text << "player=" << info.playerPosition.x << " " << info.playerPosition.y << " " << info.playerPosition.z << "\n";
		text << "look=" << info.yaw << " " << info.pitch << "\n";
	}

	std::string str = text.str();
	return writeFileAtomically(infoPath(info.name), std::vector<uint8_t>(str.begin(), str.end()));
}

bool WorldSave::readInfo(const std::string& name, WorldInfo& info) {
	std::vector<uint8_t> data;
	if (!readWholeFile(infoPath(name), data)) return false;

	WorldInfo result;
	result.name = name;//the folder name is the real name
	bool hasSeed = false;

	std::istringstream text(std::string(data.begin(), data.end()));
	std::string line;
	while (std::getline(text, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos) continue;
		std::string key = line.substr(0, equals);
		std::istringstream value(line.substr(equals + 1));

		if (key == "seed") {
			hasSeed = static_cast<bool>(value >> result.seed);
		}
		else if (key == "player") {
			result.hasPlayer = static_cast<bool>(value >> result.playerPosition.x >> result.playerPosition.y >> result.playerPosition.z);
		}
		else if (key == "look") {
			value >> result.yaw >> result.pitch;
		}
	}

	if (!hasSeed) {
		std::cerr << "world " << name << " has no seed, can't load it" << std::endl;
		return false;
	}
	info = result;
	return true;
}
