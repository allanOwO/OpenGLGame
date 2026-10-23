#pragma once
#include <string>

// Player options, saved as key=value lines in settings.txt
// Adding a setting: add the field with a default, then a line in load() and save()
// Old files without the new key just keep the default, so nothing breaks
struct Settings {
	static constexpr int minRenderDistance = 4;
	static constexpr int maxRenderDistance = 64;

	// Graphics
	int renderDistance = 16;//in chunks

	bool load(const std::string& path);//false if the file doesn't exist yet (defaults are kept)
	bool save(const std::string& path) const;
};
