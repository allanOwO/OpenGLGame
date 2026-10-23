#include "Settings.h"
#include <algorithm>
#include <fstream>
#include <iostream>

static std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	size_t end = s.find_last_not_of(" \t\r\n");
	return start == std::string::npos ? "" : s.substr(start, end - start + 1);
}

bool Settings::load(const std::string& path) {
	std::ifstream file(path);
	if (!file) return false;

	std::string line;
	while (std::getline(file, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos || line[0] == '#') continue;//comment or not a setting
		std::string key = trim(line.substr(0, equals));
		std::string value = trim(line.substr(equals + 1));

		try {
			if (key == "renderDistance") {
				renderDistance = std::clamp(std::stoi(value), minRenderDistance, maxRenderDistance);
			}
			// unknown keys are ignored, so files from newer versions still load
		}
		catch (const std::exception&) {
			std::cerr << "settings: bad value for " << key << ", using default" << std::endl;
		}
	}
	return true;
}

bool Settings::save(const std::string& path) const {
	std::ofstream file(path);
	if (!file) return false;

	file << "# Game settings, edited from the settings menu\n";
	file << "renderDistance=" << renderDistance << "\n";
	return static_cast<bool>(file);
}
