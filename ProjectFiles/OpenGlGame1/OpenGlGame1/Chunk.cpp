#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/vec2.hpp>

#include "Main.h"//need for full main deffinition


Chunk::Chunk(glm::vec3 position,int seed, Main* m)
	: chunkPosition(position),main(m),fullRebuildNeeded(true),blocks(chunkSize* chunkHeight* chunkSize, BlockType::AIR),
		VAO(0), VBO(0), EBO(0) {

	size_t maxBlocks = chunkSize * chunkHeight * chunkSize; // Worst case: fully solid
	size_t expectedBlocks = chunkSize * (chunkHeight / 2) * chunkSize; // Assume half height
	blocks.reserve(std::min(expectedBlocks, blocks.max_size() / 2));

	//use noise, with caching, from main
	if (!main) std::cerr << "main nullptr" << "\n";
	
	for (int x = 0; x < chunkSize; x++) { 
		for (int z = 0; z < chunkSize; z++) {

			float worldX = chunkPosition.x + x;
			float worldZ = chunkPosition.z + z;

			glm::ivec2 noiseKey(worldX, worldZ);
			float height; 

			std::lock_guard<std::mutex> lock(main->noiseMutex);
			//if noise cached
			if (main->noiseCache.find(noiseKey) != main->noiseCache.end()) {
				height = main->noiseCache[noiseKey];
			}
			else//compute noise and cache
			{
				height = main->noiseGen.GetNoise(worldX, worldZ);
				main->noiseCache[noiseKey] = height;
			}
			
			//height = main->noiseGen.GetNoise(worldX, worldZ);

			int terrainHeight = baseTerrainHeight + static_cast<int>((height + 1.0f) * 0.5f * (maxTerrainHeight - 1));

			for (int y = 0; y < chunkHeight; y++) {
				BlockType type; 

				//dont add block type for air as its air by default
				if (y > terrainHeight) {
					continue;
				}
				if (y == terrainHeight) {
					type = BlockType::GRASS;
				}
				else if (y >= terrainHeight - 3) {
					type = BlockType::DIRT;
				}
				else {
					type = BlockType::STONE;
				}

				size_t index = getBlockIndex(x, y, z);
				blocks[index] = type;
			}
		}
	}
}

Chunk::~Chunk() {
	if (VAO != 0) glDeleteVertexArrays(1, &VAO);
	if (VBO != 0) glDeleteBuffers(1, &VBO); 
	if (EBO != 0) glDeleteBuffers(1, &EBO); 
}

void Chunk::initializeBuffers() {
	if (VAO != 0 || VBO != 0 || EBO != 0) {
		// Already initialized
		return;
	}

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBindVertexArray(0);

	GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		std::cerr << "OpenGL Error after initializing buffers for chunk "
			<< ": " << error << std::endl;
	}
}

/*
void Chunk::generateMesh() {

	verticesByType.clear();
	indicesByType.clear();
	baseIndicesByType.clear();

	//itterate existing blocks, excludes air
	for (const auto& b : blocks) {
		// .second means the second item in the map, so here it is block 
		generateBlockFaces(verticesByType[b.second], indicesByType[b.second], b.first); 
	}

	// Set up VAO/VBO/EBO for the entire chunk
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	// Concatenate all vertices into one VBO, but track offsets
	std::vector<float> allVertices;
	std::vector<unsigned int> allIndices;
	unsigned int vertexOffset = 0;
	unsigned int indexOffset = 0;

	for (auto& pair : verticesByType) {
		BlockType type = pair.first;
		auto& vertices = pair.second;
		auto& indices = indicesByType[type];

		baseIndicesByType[type] = indexOffset;

		allVertices.insert(allVertices.end(), vertices.begin(), vertices.end());

		for (unsigned int idx : indices) {
			allIndices.push_back(idx + vertexOffset / 11); // 11 floats per vertex
		}

		vertexOffset += vertices.size();
		indexOffset += indices.size();
	}

	//std::cout << "Total vertices stored: " << allVertices.size() / 11 << std::endl;
	//std::cout << "Total float size (MB): " << (allVertices.size() * sizeof(float)) / (1024.0f * 1024.0f) << " MB\n";

	std::cout << "Chunk at " << chunkPosition.x << "," << chunkPosition.z 
		<< ": Vertices: " << allVertices.size() / 11 << " (" << allVertices.size() * 4 / 1024 << " KB), " 
		<< "Indices: " << allIndices.size() << " (" << allIndices.size() * 4 / 1024 << " KB)" << std::endl; 


	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(float), allVertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(unsigned int), allIndices.data(), GL_STATIC_DRAW);

	// Vertex attributes
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0); // Position 
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float))); // Color 
	glEnableVertexAttribArray(1); 
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float))); // Texture
	glEnableVertexAttribArray(2); 
	glVertexAttribPointer(3, 3, GL_HALF_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(8 * sizeof(float))); // Normal
	glEnableVertexAttribArray(3); 

	glBindVertexArray(0);
}
*/

MeshData Chunk::generateMeshData() {
	cacheNeighbors();
	MeshData meshData;

	for (int x = 0; x < chunkSize; x++) {
		for (int y = 0; y < chunkHeight; y++) {
			for (int z = 0; z < chunkSize; z++) {
				size_t index = getBlockIndex(x, y, z);
				if (blocks[index] == BlockType::AIR) continue; // Skip air blocks

				// Basic occlusion culling: skip fully surrounded blocks
				bool isSurrounded = true;
				for (int dx = -1; dx <= 1; dx += 2) {
					if (!isBlockSolid(x + dx, y, z)) { isSurrounded = false; break; }
				}
				if (isSurrounded) {
					for (int dy = -1; dy <= 1; dy += 2) {
						if (!isBlockSolid(x, y + dy, z)) { isSurrounded = false; break; }
					}
				}
				if (isSurrounded) {
					for (int dz = -1; dz <= 1; dz += 2) {
						if (!isBlockSolid(x, y, z + dz)) { isSurrounded = false; break; }
					}
				}
				if (isSurrounded) continue; // Skip fully occluded blocks

				std::vector<float>& vertices = meshData.verticesByType [blocks[index]];
				std::vector<unsigned int>& indices = meshData.indicesByType[blocks[index]];
				generateBlockFaces(vertices, indices, glm::ivec3(x,y,z));
			}
		}
	}
	return meshData;
}

void Chunk::generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const glm::ivec3 blockPos) {

	//block position in chunk local space
	glm::vec3 pos = blockPos;

	float size = 1.0f;
	glm::vec3 color(0.5f, 0.5f, 0.5f);//default colour
	if (pos.x == 0 || pos.x == chunkSize - 1 || pos.z == 0 || pos.z == chunkSize - 1) {
		color = glm::vec3(1.0f, 0.0f, 0.0f); // Red for border blocks
	}

	// Cube vertices positions (8 unique)
	glm::vec3 cubeVertices[8] = {
		{pos.x, pos.y, pos.z}, {pos.x + size, pos.y, pos.z},
		{pos.x + size, pos.y + size, pos.z}, {pos.x, pos.y + size, pos.z},
		{pos.x, pos.y, pos.z + size}, {pos.x + size, pos.y, pos.z + size},
		{pos.x + size, pos.y + size, pos.z + size}, {pos.x, pos.y + size, pos.z + size}
	};

	// Correct UVs per face (not per-vertex)
	float texCoords[4][2] = {
		{0.0f, 0.0f}, {1.0f, 0.0f},
		{1.0f, 1.0f}, {0.0f, 1.0f}
	};

	// Normals per face
	glm::vec3 normals[6] = {
		{0.0f, 0.0f, -1.0f}, // Front
		{0.0f, 0.0f, 1.0f},  // Back
		{-1.0f, 0.0f, 0.0f}, // Left
		{1.0f, 0.0f, 0.0f},  // Right
		{0.0f, 1.0f, 0.0f},  // Top
		{0.0f, -1.0f, 0.0f}  // Bottom
	}; 

	// Face definitions (6 faces, each using 4 unique vertices)
	int faces[6][4] = {
	{3, 2, 1, 0}, // Front
	{6, 7, 4, 5}, // Back
	{7, 3, 0, 4}, // Left
	{2, 6, 5, 1}, // Right
	{7, 6, 2, 3}, // Top
	{0, 1, 5, 4}  // Bottom
	};

	unsigned int baseIndex = vertices.size() / 11;

	// Add vertices per face
	for (int f = 0; f < 6; f++) {
		int* face = faces[f];

		// Check if the face should be culled (hidden)
		bool cullFace = false;

		// Convert to local chunk space for safe indexing
		int localX = int(pos.x);
		int localY = int(pos.y);
		int localZ = int(pos.z);
		if (localX < 0) localX += chunkSize;
		if (localY < 0) localY += chunkHeight;
		if (localZ < 0) localZ += chunkSize;

		// Check neighboring faces using chunk-relative indexing
		switch (f) {
		case 0: cullFace = isBlockSolid(localX, localY, localZ - 1); break; // Front
		case 1: cullFace = isBlockSolid(localX, localY, localZ + 1); break; // Back		
		case 2: cullFace = isBlockSolid(localX - 1, localY, localZ); break; // Left
		case 3: cullFace = isBlockSolid(localX + 1, localY, localZ); break; // Right
		case 4: cullFace = isBlockSolid(localX, localY + 1, localZ); break; // Top
		case 5: cullFace = isBlockSolid(localX, localY - 1, localZ); break; // Bottom
		}


		// Skip adding the face if it is culled
		if (cullFace) continue;

		// Add 4 vertices for the current face
		for (int i = 0; i < 4; i++) {
			vertices.push_back(cubeVertices[face[i]].x);
			vertices.push_back(cubeVertices[face[i]].y);
			vertices.push_back(cubeVertices[face[i]].z);
			vertices.push_back(color.r);
			vertices.push_back(color.g);
			vertices.push_back(color.b);
			vertices.push_back(texCoords[i][0]);
			vertices.push_back(texCoords[i][1]);
			vertices.push_back(normals[f].x); // Add normal
			vertices.push_back(normals[f].y);
			vertices.push_back(normals[f].z); 
		} 

		// Add two triangles per face
		indices.push_back(baseIndex);
		indices.push_back(baseIndex + 1);
		indices.push_back(baseIndex + 2);

		indices.push_back(baseIndex);
		indices.push_back(baseIndex + 2);
		indices.push_back(baseIndex + 3);

		baseIndex += 4; // Move index forward (4 per face)

		
	}
}

bool Chunk::isBlockSolid(int x, int y, int z) {

	if (y < 0 || y >= chunkHeight) {
		return false;
	}

	glm::ivec3 pos(x, y, z); 

	if (x >= 0 && x < chunkSize && z >= 0 && z < chunkSize) {//if in chunk
		size_t index = getBlockIndex(x, y, z); 
		return blocks[index] != BlockType::AIR; 
	}
	

	// Position is outside this chunk, find the neighboring chunk
	Chunk* neighbor = nullptr; 
	int localX = x;
	int localZ = z; 
	if (x < 0) { 
		neighbor = neighbors[1]; // -X 
		localX = x + chunkSize; 
	}
	else if (x >= chunkSize) {
		neighbor = neighbors[0]; // +X 
		localX = x - chunkSize; 
	}
	if (z < 0) {
		neighbor = neighbors[3]; // -Z 
		localZ = z + chunkSize; 
	}
	else if (z >= chunkSize) { 
		neighbor = neighbors[2]; // +Z
		localZ = z - chunkSize; 
	}
	if (!neighbor) {
		return false; // No chunk exists, assume air
	}

	// Check the block in the neighboring chunk
	size_t index = neighbor->getBlockIndex(localX, y, localZ);
	return neighbor->blocks[index] != BlockType::AIR;
}

void Chunk::setBlock(int x, int y, int z, BlockType type) {
	if (x < 0 || x >= chunkSize || y < 0 || y >= chunkHeight || z < 0 || z >= chunkSize) return;//if outside chunk

	size_t index = getBlockIndex(x, y, z); 
	blocks[index] = type; 
	fullRebuildNeeded = true;  
}

// Helper to get the index in the 1D array from 3D coordinates
size_t Chunk::getBlockIndex(int x, int y, int z) const {
	return (y * chunkSize * chunkSize) + (z * chunkSize) + x;
}

void Chunk::cacheNeighbors() { 
	neighbors[0] = main->getChunk(chunkPosition + glm::vec3(chunkSize, 0, 0));  // +X
	neighbors[1] = main->getChunk(chunkPosition + glm::vec3(-chunkSize, 0, 0)); // -X
	neighbors[2] = main->getChunk(chunkPosition + glm::vec3(0, 0, chunkSize));  // +Z
	neighbors[3] = main->getChunk(chunkPosition + glm::vec3(0, 0, -chunkSize)); // -Z
}