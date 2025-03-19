#pragma once
#include<vector>
#include<glm/glm.hpp>

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
	static const int chunkSize = 16;

	Chunk(int x, int y, int z);//constructor
	
	void generateMesh();//creates chunk mesh
	void render();//renders chunk

	unsigned int VBO, VAO, EBO;
	std::vector<unsigned int>indices;//index data for rendering

private:
	int chunkX, chunkY, chunkZ;//chunk position
	std::vector<Block> blocks;//list of all blocks in chunk
	
	

	void generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block); 
};

