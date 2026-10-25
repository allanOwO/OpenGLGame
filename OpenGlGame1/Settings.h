#pragma once
#include <string>

// Player options, saved as key=value lines in settings.txt
// Adding a setting: add the field with a default, then a line in load() and save()
// Old files without the new key just keep the default, so nothing breaks
struct Settings {
	static constexpr int minRenderDistance = 4;
	static constexpr int maxRenderDistance = 200;

	// Graphics
	int renderDistance = 16;//in chunks
	// Where each level of detail starts, as a percentage of render distance (100 = never)
	// Always in order: 2x starts before 4x, 4x before 8x (Menu keeps them that way)
	int lod2xPercent = 15;
	int lod4xPercent = 30;
	int lod8xPercent = 60;

	bool load(const std::string& path);//false if the file doesn't exist yet (defaults are kept)
	bool save(const std::string& path) const;
};
