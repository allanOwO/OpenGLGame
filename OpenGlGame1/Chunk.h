#pragma once
#include<vector>
#include<glm/glm.hpp>
#include <glad/glad.h>
#include<GLFW/glfw3.h>
#include "BlockType.h"
#include "MeshData.h"

#include "VertexPacking.h"
#include <glm/packing.hpp>


class TerrainGenerator;//forward declaration, Chunk only needs a reference

class Chunk
{
public:
	static constexpr int chunkSize = 16;
	static constexpr int chunkHeight = 256;
	static constexpr int baseTerrainHeight = 64;
	int currentTallestBlock = 0;//for fustrum culling , avoids it detecting air as in culling view
	bool isActive = true;

	Chunk(glm::ivec3 position, const TerrainGenerator& terrain);//constructor, generates terrain (safe to run on a worker thread)
	Chunk(glm::ivec3 position, std::vector<BlockType> savedBlocks);//restores a chunk the player modified earlier
	~Chunk();
	// Delete copy constructor and copy assignment operator
	Chunk(const Chunk&) = delete;
	Chunk& operator=(const Chunk&) = delete;
	// Move transfers the GL buffers so they are only deleted once
	Chunk(Chunk&& other) noexcept;
	Chunk& operator=(Chunk&& other) noexcept;

	void setBlock(int x, int y, int z, BlockType type);
	BlockType getBlock(int x, int y, int z) const;
	// Helper to get the index in the 1D array from 3D coordinates
	size_t getBlockIndex(int x, int y, int z) const;

	//copies this chunk + neighbour borders so the mesh can be built on another thread
	//neighbours are +X, -X, +Z, -Z, then the diagonals +X+Z, +X-Z, -X+Z, -X-Z (ambient occlusion needs the corners)
	//nullptr if not loaded (treated as air)
	ChunkMeshInput makeMeshInput(const Chunk* const neighbours[8]) const;
	static MeshData buildMesh(const ChunkMeshInput& input);//greedy meshing with ambient occlusion

	void initializeBuffers(); // create VAO/VBO/EBO, main thread only
	void uploadMesh(const MeshData& mesh); // main thread only

	unsigned int VBO = 0, VAO = 0, EBO = 0;
	GLsizei indexCount = 0;
	glm::vec3 chunkPosition;

	std::vector<BlockType> blocks;//dense array, uses more ram, quicker lookup
	bool fullRebuildNeeded = true;
	bool modified = false;//player changed a block, so it must be kept when unloaded (it can't be regenerated)

private:
	void releaseBuffers();
};

