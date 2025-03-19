#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

Chunk::Chunk(int x, int y, int z) : chunkX(x), chunkY(y), chunkZ(z) {
	//initialize chunk blocks
	for (int i = 0;i < chunkSize;i++) {//y
		for (int j = 0;j < chunkSize;j++) {//x
			for (int k = 0;k < chunkSize;k++) {//z
				blocks.push_back({ BlockType::DIRT,glm::vec3(i,j,k) });
			}
		}
	}
}

void Chunk::generateMesh() {
	std::vector<float> vertices;
	indices.clear();

	//loop for all blocks
	for (int y = 0;y < chunkSize;y++) {//y
		for (int x = 0;x < chunkSize;x++) {//x
			for (int z = 0;z < chunkSize;z++) {//z
				Block& block = blocks[y * chunkSize * chunkSize + x * chunkSize + z];
				if (block.type == BlockType::AIR) continue;//dont draw faces for air

				generateBlockFaces(vertices, indices, block);//create each blocks faces
			}
		}
	}
	//create buffers
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	// Position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Color attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); 
	glEnableVertexAttribArray(1); 

	// Texture coordinate attribute
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); 
	glEnableVertexAttribArray(2); 

	glBindVertexArray(0); // Unbind VAO}

	
}

void Chunk::generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block) {
	glm::vec3 pos = block.position;
	float size = 1.0f;
	glm::vec3 color(0.6f, 0.4f, 0.2f); // Brown color for dirt

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
		{0, 1, 2, 3}, // Front
		{5, 4, 7, 6}, // Back
		{4, 0, 3, 7}, // Left
		{1, 5, 6, 2}, // Right
		{3, 2, 6, 7}, // Top
		{4, 5, 1, 0}  // Bottom
	};

	unsigned int baseIndex = vertices.size() / 8;

	// Add vertices per face
	for (int f = 0; f < 6; f++) {
		int* face = faces[f];

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
