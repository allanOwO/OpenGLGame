#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/vec2.hpp>

#include "Main.h"//need for full main deffinition
#include "BlockConstants.h"


Chunk::Chunk(glm::ivec3 position,int seed, Main* m)
	: chunkPosition(position),main(m),fullRebuildNeeded(true),blocks(chunkSize* chunkHeight* chunkSize, BlockType::AIR), currentTallestBlock(0),
		VAO(0), VBO(0), EBO(0) {

	size_t maxBlocks = chunkSize * chunkHeight * chunkSize; // Worst case: fully solid
	size_t expectedBlocks = chunkSize * (chunkHeight / 2) * chunkSize; // Assume half height
	blocks.reserve(std::min(expectedBlocks, blocks.max_size() / 2));

	//use noise, with caching, from main
	if (!main) std::cerr << "main nullptr" << "\n";
	 
	std::lock_guard<std::mutex> lock(main->noiseMutex); 

	for (int x = 0; x < chunkSize; x++) { 
		for (int z = 0; z < chunkSize; z++) {

			float worldX = chunkPosition.x + x;
			float worldZ = chunkPosition.z + z;

			float height = main->getNoise(static_cast<float>(worldX), static_cast<float>(worldZ));//returns cached noise or gens new noise
			
			//height = main->noiseGen.GetNoise(worldX, worldZ);

			//int terrainHeight = baseTerrainHeight + static_cast<int>((height + 1.0f) * 0.5f * (maxTerrainHeight - 1));
			int terrainHeight = height;
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
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glGenVertexArrays: " << err << std::endl;

	glGenBuffers(1, &VBO);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glGenBuffers (VBO): " << err << std::endl;

	glGenBuffers(1, &EBO);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glGenBuffers (EBO): " << err << std::endl;

	glBindVertexArray(VAO);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glBindVertexArray: " << err << std::endl;

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glBindBuffer (VBO): " << err << std::endl;

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after glBindBuffer (EBO): " << err << std::endl;

	glBindVertexArray(0);
	err = glGetError();
	if (err != GL_NO_ERROR) std::cerr << "Error after unbinding VAO: " << err << std::endl; 

	GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		std::cerr << "OpenGL Error after initializing buffers for chunk "
			<< ": " << error << std::endl;
	}
}

MeshData Chunk::generateMeshData() {
	cacheNeighbors();
	MeshData meshData;

	int localMaxHeight = 0; // Maximum Y value of non-air blocks in this chunk

	// Step 1: Counting pass to determine visible faces per block type
	std::map<BlockType, int> faceCount;

	for (int x = 0; x < chunkSize; x++) {
		for (int y = 0; y < chunkHeight; y++) {
			for (int z = 0; z < chunkSize; z++) {
				size_t index = getBlockIndex(x, y, z);
				if (blocks[index] == BlockType::AIR) continue; // Skip air blocks

				BlockType type = blocks[index];

				// Check visibility of each face
				if (!isBlockSolid(x + 0, y + 0, z - 1)) faceCount[type]++; // Front 
				if (!isBlockSolid(x + 0, y + 0, z + 1)) faceCount[type]++; // Back
				if (!isBlockSolid(x - 1, y + 0, z + 0)) faceCount[type]++; // Left  
				if (!isBlockSolid(x + 1, y + 0, z + 0)) faceCount[type]++; // Right 
				if (!isBlockSolid(x + 0, y + 1, z + 0)) faceCount[type]++; // Top 
				if (!isBlockSolid(x + 0, y - 1, z + 0)) faceCount[type]++; // Bottom 

				// Update max height
				localMaxHeight = std::max(localMaxHeight, y);
			}
		}
	} 

	//Step 2: Reserve space in vectors based on face counts
    for (const auto& pair : faceCount) {
        BlockType type = pair.first;
        int faces = pair.second;
        meshData.packedVerticesByType[type].resize(faces * 4); // 4 vertices 
        meshData.indicesByType[type].resize(faces * 6);       // 6 indices per face
    }

	//gen mesh using pointers
	for (auto& pair : meshData.packedVerticesByType) {
		BlockType type = pair.first;
		std::vector<PackedVertex>& vertices = pair.second; 
		std::vector<unsigned int>& indices = meshData.indicesByType[type];

		//pointers to the start of the vectors
		PackedVertex* vertexPtr = vertices.data();
		unsigned int* indexPtr = indices.data();
		unsigned int baseVertexIndex = 0;

		for (int x = 0; x < chunkSize; x++) {
			for (int y = 0; y < chunkHeight; y++) {
				for (int z = 0; z < chunkSize; z++) {
					size_t index = getBlockIndex(x, y, z);
					if (blocks[index] != type || blocks[index] == BlockType::AIR) continue;

					// Basic occlusion culling: skip fully surrounded blocks
					bool isSurrounded = true;
					for (int dx = -1; dx <= 1 && isSurrounded; dx += 2) {
						if (!isBlockSolid(x + dx, y, z)) { isSurrounded = false; break; }
					}
					if (isSurrounded) {
						for (int dy = -1; dy <= 1 && isSurrounded; dy += 2) {
							if (!isBlockSolid(x, y + dy, z)) { isSurrounded = false; break; }
						}
					}
					if (isSurrounded) {
						for (int dz = -1; dz <= 1 && isSurrounded; dz += 2) {
							if (!isBlockSolid(x, y, z + dz)) { isSurrounded = false; break; }
						}
					}
					if (isSurrounded) continue;

					// Generate faces for this block using pointers
					generateBlockFaces(vertexPtr, indexPtr, baseVertexIndex, glm::ivec3(x, y, z), type);
				}
			}
		}
	}
	currentTallestBlock = localMaxHeight;
	return meshData;
}

void Chunk::generateBlockFaces(PackedVertex*& vertexPtr, unsigned int*& indexPtr, unsigned int& baseVertexIndex, const glm::ivec3 blockPos,const BlockType& type) {

	//block position in chunk local space  
	glm::vec3 pos = blockPos;
	float size = 1.0f;
	glm::vec3 colour(1.0f, 1.0f, 1.0f);//default colour
	//if (pos.x == 0 || pos.x == chunkSize - 1 || pos.z == 0 || pos.z == chunkSize - 1) {
		//colour = glm::vec3(1.0f, 0.0f, 0.0f); // Red for border blocks
	//}

	// Create an array of cube vertices for this block using the precomputed unit cube vertices.
	// For each vertex, add the block position to the unit cube vertex, scaled by size. 
	glm::vec3 cubeVertices[8]; 
	for (int i = 0; i < 8; i++) { 
		cubeVertices[i] = unitCubeVertices[i] * size + pos; 
	}

	// Add vertices per face
	for (int f = 0; f < 6; f++) {

		// Check if the face should be culled (hidden)
		bool cullFace = false;

		// Convert to local chunk space for safe indexing
		int16_t localX = static_cast<int16_t>(pos.x);
		int16_t localY = static_cast<int16_t>(pos.y);
		int16_t localZ = static_cast<int16_t>(pos.z);

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

		BlockUV atlasUV = getBlockUV(type, faceEnum[f]);


	   // Determine the four vertices for the face.
	   // The order: bottom-left, bottom-right, top-right, top-left.
		PackedVertex faceVerts[4];
		for (int i = 0; i < 4; i++) {
			glm::vec3 vertexPos = cubeVertices[faces[f][i]];
			// Pack position.
			faceVerts[i].pos[0] = static_cast<int16_t>(vertexPos.x); 
			faceVerts[i].pos[1] = static_cast<int16_t>(vertexPos.y); 
			faceVerts[i].pos[2] = static_cast<int16_t>(vertexPos.z); 
			// Pack colour.
			faceVerts[i].colour = packColor(colour);
			// Pack texture coordinates.
			packTexCoord(texCoords[i], faceVerts[i].tex, atlasUV);
			// Pack normal: use the precomputed normal for face f.
			faceVerts[i].normal = packNormal(normals[f]);
		}

		// Write the 4 vertices into our output pointer.
		for (int i = 0; i < 4; i++) {
			*vertexPtr = faceVerts[i];
			vertexPtr++;
		}

		// Write 6 indices (two triangles) for this face
		*indexPtr++ = baseVertexIndex;     // Triangle 1
		*indexPtr++ = baseVertexIndex + 1;
		*indexPtr++ = baseVertexIndex + 2;
		*indexPtr++ = baseVertexIndex;     // Triangle 2
		*indexPtr++ = baseVertexIndex + 2;
		*indexPtr++ = baseVertexIndex + 3;

		baseVertexIndex += 4; // Increment for the next face		
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

	//technically incorrect as there could be >1 blocks at max height
	if (type == BlockType::AIR)//if broken was tallest, lower tallest block to broken
		currentTallestBlock = std::min(currentTallestBlock, y - 1);
	else
		currentTallestBlock = std::max(currentTallestBlock, y);

	blocks[index] = type; 
	fullRebuildNeeded = true;  

	// Notify neighboring chunks if a block is placed at the edge
	if (x == chunkSize - 1 && neighbors[0]) neighbors[0]->fullRebuildNeeded = true;//x+
	if (x == 0 && neighbors[1]) neighbors[1]->fullRebuildNeeded = true;//x-

	if (z == chunkSize - 1 && neighbors[2]) neighbors[2]->fullRebuildNeeded = true;//z+
	if (z == 0 && neighbors[3]) neighbors[3]->fullRebuildNeeded = true;//z-
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