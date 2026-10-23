#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// Everything about a world except its blocks, stored as key=value lines in world.txt
struct WorldInfo {
	std::string name;
	int seed = 0;
	bool hasPlayer = false;//false for a brand new world, the player spawns on the surface instead
	glm::vec3 playerPosition = glm::vec3(0.0f);//feet position
	float yaw = -90.0f;
	float pitch = 0.0f;
};

// Where worlds live on disk:
//   saves/<world name>/world.txt   - WorldInfo (text, easy to read and fix by hand)
//   saves/<world name>/chunks.dat  - every chunk the player changed (binary, see ChunkStorage)
namespace WorldSave {
	constexpr size_t maxNameLength = 24;

	bool isNameCharacter(char c);//letters, digits, space, - and _
	bool isValidName(const std::string& name);//non empty, allowed characters, no space at either end
	bool exists(const std::string& name);

	std::string worldFolder(const std::string& name);
	std::string chunksPath(const std::string& name);

	// Names of saved worlds, most recently played first
	std::vector<std::string> listWorlds();

	bool writeInfo(const WorldInfo& info);//creates the world folder if needed
	bool readInfo(const std::string& name, WorldInfo& info);
}
