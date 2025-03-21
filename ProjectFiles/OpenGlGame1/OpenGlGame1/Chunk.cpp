#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

Chunk::Chunk(glm::vec3 position)
	: chunkPosition(position) {

	/// Initialize chunk blocks using 3D vector storage
	blocks.resize(chunkSize, std::vector<std::vector<Block>>(chunkSize, std::vector<Block>(chunkSize))); 

	for (int i = 0;i < chunkSize;i++) {//y
		for (int j = 0;j < chunkSize;j++) {//x
			for (int k = 0;k < chunkSize;k++) {//z

				BlockType type;
				if (i < 10)
					type = BlockType::STONE;

				else if (i < chunkSize - 1)
					type = BlockType::DIRT;
				else
					type = BlockType::GRASS;

				blocks[j][i][k] = {type,glm::vec3(j,i,k)};//j,i,k = x,y,z
			}
		}
	}
}

Chunk::~Chunk() {
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
}

void Chunk::generateMesh() {
	verticesByType.clear();
	indicesByType.clear();
	baseIndicesByType.clear();

	for (int y = 0; y < chunkSize; y++) {
		for (int x = 0; x < chunkSize; x++) {
			for (int z = 0; z < chunkSize; z++) {
				Block& block = blocks[x][y][z];
				if (block.type == BlockType::AIR) continue;
				generateBlockFaces(verticesByType[block.type], indicesByType[block.type], block);
			}
		}
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
			allIndices.push_back(idx + vertexOffset / 8); // 8 floats per vertex
		}

		vertexOffset += vertices.size();
		indexOffset += indices.size();
	}

	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(float), allVertices.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(unsigned int), allIndices.data(), GL_STATIC_DRAW);

	// Vertex attributes
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
}

void Chunk::generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block) {

	//block position in chunk local space
	glm::vec3 pos = block.position;

	float size = 1.0f;
	glm::vec3 color(0.5f, 0.5f, 0.5f);//default colour

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

	// Face definitions (6 faces, each using 4 unique vertices)
	int faces[6][4] = {
	{3, 2, 1, 0}, // Front
	{6, 7, 4, 5}, // Back
	{7, 3, 0, 4}, // Left
	{2, 6, 5, 1}, // Right
	{7, 6, 2, 3}, // Top
	{0, 1, 5, 4}  // Bottom
	};

	unsigned int baseIndex = vertices.size() / 8;

	// Add vertices per face
	for (int f = 0; f < 6; f++) {
		int* face = faces[f];

		// Check if the face should be culled (hidden)
		bool cullFace = false;

		// Convert to local chunk space for safe indexing
		int localX = int(pos.x) % chunkSize;
		int localY = int(pos.y) % chunkSize;
		int localZ = int(pos.z) % chunkSize;
		if (localX < 0) localX += chunkSize;
		if (localY < 0) localY += chunkSize;
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
	if (x < 0 || x >= chunkSize || y < 0 || y >= chunkSize || z < 0 || z >= chunkSize)
		return false; // Treat out-of-bounds as empty space
	return blocks[x][y][z].type != BlockType::AIR;
}
