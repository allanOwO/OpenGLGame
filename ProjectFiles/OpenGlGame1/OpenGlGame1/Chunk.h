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

	Chunk(glm::vec3 position);//constructor
	~Chunk();
	
	void generateMesh();//creates chunk mesh

	unsigned int VBO, VAO, EBO;

	std::map<BlockType, std::vector<float>> verticesByType; 
	std::map<BlockType, std::vector<unsigned int>> indicesByType; 
	std::map<BlockType, unsigned int> baseIndicesByType; // Track base index per type 
	glm::vec3 chunkPosition;


private:
	
	std::vector<std::vector<std::vector<Block>>> blocks;//list of all blocks in chunk

	void generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block); 
	bool isBlockSolid(int x, int y, int z);
};

