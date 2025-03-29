#pragma once
#include<vector>
#include<glm/glm.hpp>
#include<map>
#include <glad/glad.h>
#include<GLFW/glfw3.h>
#include "Vec3Hash.h"
#include <unordered_map>
#include "BlockType.h"
#include "MeshData.h"

#include "VertexPacking.h"
#include <glm/packing.hpp> 


struct Block
{
	BlockType type;
};

class Main;//forward declaration

class Chunk
{
	Main* main;//main pointer

public:
	static constexpr int chunkSize = 16;
	static constexpr int chunkHeight = 256;
	static constexpr int maxTerrainHeight = 32;
	static constexpr int baseTerrainHeight = 64;
	int currentTallestBlock;//for fustrum culling , avoids it detecting air as in culling view
	bool isActive = true;

	Chunk(glm::ivec3 position, int seed, Main* m = nullptr);//constructor
	~Chunk();
	// Delete copy constructor and copy assignment operator
	Chunk(const Chunk&) = delete; 
	Chunk& operator=(const Chunk&) = delete; 
	// Define move constructor
	Chunk(Chunk&& other) noexcept
		: chunkPosition(other.chunkPosition),main(other.main),
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

	//void generateMesh();//creates chunk mesh
	MeshData generateMeshData();
	void setBlock(int x, int y, int z, BlockType type); 
	// Helper to get the index in the 1D array from 3D coordinates
	size_t getBlockIndex(int x, int y, int z) const;

	//void updateMesh();

	unsigned int VBO, VAO, EBO;

	std::map < BlockType, std::vector <PackedVertex>> verticesByType;//vertexes packed to 16bytes
	std::map<BlockType, std::vector<unsigned int>> indicesByType; 
	std::map<BlockType, unsigned int> baseIndicesByType; // Track base index per type 
	glm::vec3 chunkPosition;

	std::vector<BlockType> blocks;//dense array, uses more ram, quicker lookup  
	bool fullRebuildNeeded = true;

	void initializeBuffers(); // New method to initialize OpenGL buffers 

private:
	
	void generateBlockFaces(PackedVertex*& vertexPtr, unsigned int*& indexPtr, unsigned int& baseVertexIndex, const glm::ivec3 blockPos);
	bool isBlockSolid(int x, int y, int z); 
	void cacheNeighbors();
 
	Chunk* neighbors[4]; // +X, -X, +Z, -Z 

	//pre made faces, texcoords, and normals, saves re making them per block
	//stored in "blockConstants.cpp"

};

