#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<map>
#include <glad/glad.h>
#include<GLFW/glfw3.h>

enum class BlockType {
	AIR,
	DIRT,
	STONE,
	GRASS,
	WATER
};

struct Block
{
	BlockType type;
	glm::vec3 position;
};
class Chunk
{
public:
	static constexpr int chunkSize = 16;
	static constexpr int chunkHeight = 128;
	static constexpr int maxTerrainHeight = 32;
	static constexpr int baseTerrainHeight = 64;

	Chunk(glm::vec3 position,int seed);//constructor
	~Chunk();
	// Delete copy constructor and copy assignment operator
	Chunk(const Chunk&) = delete; 
	Chunk& operator=(const Chunk&) = delete; 
	// Define move constructor
	Chunk(Chunk&& other) noexcept
		: chunkPosition(other.chunkPosition),
		verticesByType(std::move(other.verticesByType)),
		indicesByType(std::move(other.indicesByType)),
		baseIndicesByType(std::move(other.baseIndicesByType)),
		blocks(std::move(other.blocks)),
		VAO(other.VAO),
		VBO(other.VBO),
		EBO(other.EBO) 
	{
		// Reset the other object's buffer IDs to prevent double deletion
		other.VAO = 0;
		other.VBO = 0;
		other.EBO = 0;
	} 
	// Define move assignment operator
	Chunk& operator=(Chunk&& other) noexcept
	{
		if (this != &other) {
			// Delete existing resources
			glDeleteVertexArrays(1, &VAO);
			glDeleteBuffers(1, &VBO);
			glDeleteBuffers(1, &EBO);

			// Transfer ownership
			chunkPosition = other.chunkPosition;
			verticesByType = std::move(other.verticesByType);
			indicesByType = std::move(other.indicesByType);
			baseIndicesByType = std::move(other.baseIndicesByType);
			blocks = std::move(other.blocks);
			VAO = other.VAO;
			VBO = other.VBO;
			EBO = other.EBO;

			// Reset the other object's buffer IDs
			other.VAO = 0;
			other.VBO = 0;
			other.EBO = 0;
		}
		return *this;
	} 

	void generateMesh();//creates chunk mesh
	void setBlock(int x, int y, int z, BlockType type); 

	unsigned int VBO, VAO, EBO;

	std::map<BlockType, std::vector<float>> verticesByType; 
	std::map<BlockType, std::vector<unsigned int>> indicesByType; 
	std::map<BlockType, unsigned int> baseIndicesByType; // Track base index per type 
	glm::vec3 chunkPosition;

	std::vector<std::vector<std::vector<Block>>> blocks;//list of all blocks in chunk 

private:
	
	

	void generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block); 
	bool isBlockSolid(int x, int y, int z);
};

