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
	static const int chunkSize = 4;

	Chunk(int x, int y, int z);//constructor
	~Chunk();
	
	void generateMesh();//creates chunk mesh

	unsigned int VBO, VAO, EBO;
	std::vector<unsigned int>indices;//index data for rendering

private:
	int chunkX, chunkY, chunkZ;//chunk position
	std::vector<Block> blocks;//list of all blocks in chunk
	
	

	void generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block); 
	bool isBlockSolid(int x, int y, int z);
};

