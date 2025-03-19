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

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	// Position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0); // Unbind VAO}
}

void Chunk::generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block) {
    glm::vec3 pos = block.position;
    float size = 1.0f; // Block size

    // Define the 8 unique vertices of a cube
    glm::vec3 cubeVertices[8] = {
        {pos.x, pos.y, pos.z},                     // 0: Bottom-left-front
        {pos.x + size, pos.y, pos.z},              // 1: Bottom-right-front
        {pos.x + size, pos.y + size, pos.z},       // 2: Top-right-front
        {pos.x, pos.y + size, pos.z},              // 3: Top-left-front
        {pos.x, pos.y, pos.z + size},              // 4: Bottom-left-back
        {pos.x + size, pos.y, pos.z + size},       // 5: Bottom-right-back
        {pos.x + size, pos.y + size, pos.z + size},// 6: Top-right-back
        {pos.x, pos.y + size, pos.z + size}        // 7: Top-left-back
    };

    // Indices for each face (2 triangles per face, 6 faces)
    int faceIndices[6][6] = {
        {0, 1, 2, 2, 3, 0}, // Front
        {5, 4, 7, 7, 6, 5}, // Back
        {4, 0, 3, 3, 7, 4}, // Left
        {1, 5, 6, 6, 2, 1}, // Right
        {3, 2, 6, 6, 7, 3}, // Top
        {4, 5, 1, 1, 0, 4}  // Bottom
    };

    // Indices for two triangles per face
	unsigned int baseIndex = vertices.size() / 3;

	// Add vertices
	for (int i = 0; i < 8; i++) {
		vertices.push_back(cubeVertices[i].x);
		vertices.push_back(cubeVertices[i].y);
		vertices.push_back(cubeVertices[i].z);
	}

	// Add indices
	for (int f = 0; f < 6; f++) {
		for (int i = 0; i < 6; i++) {
			indices.push_back(baseIndex + faceIndices[f][i]);
		}
	}
}