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

	// Now you can upload the vertices and indices to OpenGL
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

	// Create and bind VAO (assuming you haven't done it already elsewhere)
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);  // Unbind VAO 
}

void Chunk::generateBlockFaces(std::vector<float>& vertices, std::vector<unsigned int>& indices, const Block& block) {
	glm::vec3 pos = block.position;
	float size = 1.0f;//each block size

    // Define the 6 faces of a cube
    glm::vec3 faceVertices[24] = { 
        { pos.x, pos.y, pos.z }, // Front face
        { pos.x + size, pos.y, pos.z },
        { pos.x + size, pos.y + size, pos.z },
        { pos.x, pos.y + size, pos.z },

        { pos.x, pos.y, pos.z + size }, // Back face
        { pos.x + size, pos.y, pos.z + size },
        { pos.x + size, pos.y + size, pos.z + size },
        { pos.x, pos.y + size, pos.z + size },

        { pos.x, pos.y, pos.z }, // Bottom face
        { pos.x + size, pos.y, pos.z },
        { pos.x + size, pos.y, pos.z + size },
        { pos.x, pos.y, pos.z + size },

        { pos.x, pos.y + size, pos.z }, // Top face
        { pos.x + size, pos.y + size, pos.z },
        { pos.x + size, pos.y + size, pos.z + size },
        { pos.x, pos.y + size, pos.z + size },

        { pos.x, pos.y, pos.z }, // Left face
        { pos.x, pos.y + size, pos.z },
        { pos.x, pos.y + size, pos.z + size },
        { pos.x, pos.y, pos.z + size },

        { pos.x + size, pos.y, pos.z }, // Right face
        { pos.x + size, pos.y + size, pos.z },
        { pos.x + size, pos.y + size, pos.z + size },
        { pos.x + size, pos.y, pos.z + size }
    }; 

    // Push the face vertices to the vector
    for (int i = 0; i < 24; ++i) {
        vertices.push_back(faceVertices[i].x);
        vertices.push_back(faceVertices[i].y);
        vertices.push_back(faceVertices[i].z);
    }

    // Define the indices for the cube faces (two triangles per face, 6 faces total)
    unsigned int baseIndex = indices.size();
    unsigned int faceIndices[36] = {
        0, 1, 2, 0, 2, 3, // front
        4, 5, 6, 4, 6, 7, // back
        8, 9, 10, 8, 10, 11, // bottom
        12, 13, 14, 12, 14, 15, // top
        16, 17, 18, 16, 18, 19, // left
        20, 21, 22, 20, 22, 23 // right
    };

    for (int i = 0; i < 36; ++i) {
        indices.push_back(baseIndex + faceIndices[i]);
    }
}

void Chunk::render() {
	glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, indices.size());
	glBindVertexArray(0);
}
