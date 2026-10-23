#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <cstddef>//offsetof

#include "TerrainGenerator.h"
#include "BlockConstants.h"

//neighbour direction for each face, same order as faces[] in BlockConstants
static const glm::ivec3 faceOffsets[6] = {
	{ 0, 0,-1}, // Front
	{ 0, 0, 1}, // Back
	{-1, 0, 0}, // Left
	{ 1, 0, 0}, // Right
	{ 0, 1, 0}, // Top
	{ 0,-1, 0}  // Bottom
};


Chunk::Chunk(glm::ivec3 position, const TerrainGenerator& terrain)
	: chunkPosition(position), blocks(chunkSize* chunkHeight* chunkSize, BlockType::AIR) {

	for (int x = 0; x < chunkSize; x++) {
		for (int z = 0; z < chunkSize; z++) {

			float worldX = chunkPosition.x + x;
			float worldZ = chunkPosition.z + z;

			float height = terrain.getHeight(worldX, worldZ);

			int terrainHeight = static_cast<int>(height);
			for (int y = 0; y < chunkHeight; y++) {
				BlockType type;

				//dont add block type for air as its air by default
				if (y > terrainHeight) {
					break;
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

				blocks[getBlockIndex(x, y, z)] = type;
				currentTallestBlock = std::max(currentTallestBlock, y);
			}
		}
	}
}

Chunk::Chunk(glm::ivec3 position, std::vector<BlockType> savedBlocks)
	: chunkPosition(position), blocks(std::move(savedBlocks)), modified(true) {
	// Still modified, if it unloads again it has to be stored again

	// Find the tallest block for frustum culling until the first mesh works it out exactly
	for (int y = chunkHeight - 1; y >= 0 && currentTallestBlock == 0; y--) {
		for (int i = 0; i < chunkSize * chunkSize; i++) {
			if (blocks[getBlockIndex(0, y, 0) + i] != BlockType::AIR) {
				currentTallestBlock = y;
				break;
			}
		}
	}
}

Chunk::~Chunk() {
	releaseBuffers();
}

Chunk::Chunk(Chunk&& other) noexcept
	: currentTallestBlock(other.currentTallestBlock), isActive(other.isActive),
	VBO(other.VBO), VAO(other.VAO), EBO(other.EBO), indexCount(other.indexCount),
	chunkPosition(other.chunkPosition), blocks(std::move(other.blocks)),
	fullRebuildNeeded(other.fullRebuildNeeded), modified(other.modified)
{
	// Reset the other object's buffer IDs to prevent double deletion
	other.VAO = 0;
	other.VBO = 0;
	other.EBO = 0;
	other.indexCount = 0;
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
	if (this != &other) {
		releaseBuffers();

		currentTallestBlock = other.currentTallestBlock;
		isActive = other.isActive;
		VBO = other.VBO;
		VAO = other.VAO;
		EBO = other.EBO;
		indexCount = other.indexCount;
		chunkPosition = other.chunkPosition;
		blocks = std::move(other.blocks);
		fullRebuildNeeded = other.fullRebuildNeeded;
		modified = other.modified;

		other.VAO = 0;
		other.VBO = 0;
		other.EBO = 0;
		other.indexCount = 0;
	}
	return *this;
}

void Chunk::releaseBuffers() {
	if (VAO != 0) glDeleteVertexArrays(1, &VAO);
	if (VBO != 0) glDeleteBuffers(1, &VBO);
	if (EBO != 0) glDeleteBuffers(1, &EBO);
	VAO = VBO = EBO = 0;
	indexCount = 0;
}

void Chunk::initializeBuffers() {
	if (VAO != 0) {
		// Already initialized
		return;
	}

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	//layout only needs setting once, uploadMesh just replaces the buffer contents
	glVertexAttribIPointer(0, 3, GL_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, pos));
	glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, colour));
	glVertexAttribIPointer(2, 2, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, tex));
	glVertexAttribIPointer(3, 1, GL_INT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, normal));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);
}

void Chunk::uploadMesh(const MeshData& mesh) {
	if (VAO == 0) {
		std::cerr << "uploadMesh called before initializeBuffers" << std::endl;
		return;
	}

	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(PackedVertex), mesh.vertices.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_DYNAMIC_DRAW);
	glBindVertexArray(0);

	indexCount = static_cast<GLsizei>(mesh.indices.size());
	currentTallestBlock = mesh.tallestBlock;
}

ChunkMeshInput Chunk::makeMeshInput(const Chunk* const neighbours[4]) const {
	constexpr int P = ChunkMeshInput::paddedSize;
	ChunkMeshInput input;
	input.blocks.assign(static_cast<size_t>(P) * P * chunkHeight, BlockType::AIR);

	// Find the real tallest block, currentTallestBlock can be stale after edits
	for (int y = chunkHeight - 1; y >= 0 && input.maxY < 0; y--) {
		const BlockType* row = &blocks[getBlockIndex(0, y, 0)];
		for (int i = 0; i < chunkSize * chunkSize; i++) {
			if (row[i] != BlockType::AIR) { input.maxY = y; break; }
		}
	}
	if (input.maxY < 0) return input;//empty chunk

	auto set = [&](int x, int y, int z, BlockType type) {
		input.blocks[(static_cast<size_t>(y) * P + (z + 1)) * P + (x + 1)] = type;
	};

	// Faces only exist up to maxY, and need to see one block above it
	int copyTop = std::min(input.maxY + 1, chunkHeight - 1);
	for (int y = 0; y <= copyTop; y++) {
		for (int z = 0; z < chunkSize; z++) {
			for (int x = 0; x < chunkSize; x++) {
				set(x, y, z, blocks[getBlockIndex(x, y, z)]);
			}
		}
		// Border column from each neighbour, missing neighbours stay air
		for (int i = 0; i < chunkSize; i++) {
			if (neighbours[0]) set(chunkSize, y, i, neighbours[0]->getBlock(0, y, i));             // +X
			if (neighbours[1]) set(-1, y, i, neighbours[1]->getBlock(chunkSize - 1, y, i));        // -X
			if (neighbours[2]) set(i, y, chunkSize, neighbours[2]->getBlock(i, y, 0));             // +Z
			if (neighbours[3]) set(i, y, -1, neighbours[3]->getBlock(i, y, chunkSize - 1));        // -Z
		}
	}
	return input;
}

MeshData Chunk::buildMesh(const ChunkMeshInput& input) {
	MeshData mesh;
	mesh.tallestBlock = std::max(input.maxY, 0);
	mesh.vertices.reserve(4096);
	mesh.indices.reserve(6144);

	// Single pass, only up to the tallest block
	for (int y = 0; y <= input.maxY; y++) {
		for (int z = 0; z < chunkSize; z++) {
			for (int x = 0; x < chunkSize; x++) {
				BlockType type = input.get(x, y, z);
				if (type == BlockType::AIR) continue;

				glm::ivec3 pos(x, y, z);
				for (int f = 0; f < 6; f++) {
					glm::ivec3 n = pos + faceOffsets[f];
					bool neighbourSolid = n.y >= 0 && n.y < chunkHeight && input.get(n.x, n.y, n.z) != BlockType::AIR;
					if (!neighbourSolid) {
						addFace(mesh, pos, f, type);
					}
				}
			}
		}
	}
	return mesh;
}

void Chunk::addFace(MeshData& mesh, const glm::ivec3& blockPos, int face, BlockType type) {
	glm::vec3 colour(1.0f, 1.0f, 1.0f);//default colour
	BlockUV atlasUV = getBlockUV(type, faceEnum[face]);
	unsigned int baseVertexIndex = static_cast<unsigned int>(mesh.vertices.size());

	// The order: bottom-left, bottom-right, top-right, top-left.
	for (int i = 0; i < 4; i++) {
		glm::vec3 vertexPos = unitCubeVertices[faces[face][i]] + glm::vec3(blockPos);
		PackedVertex vertex;
		vertex.pos[0] = static_cast<int16_t>(vertexPos.x);
		vertex.pos[1] = static_cast<int16_t>(vertexPos.y);
		vertex.pos[2] = static_cast<int16_t>(vertexPos.z);
		vertex.colour = packColor(colour);
		packTexCoord(texCoords[i], vertex.tex, atlasUV);
		vertex.normal = packNormal(normals[face]);
		mesh.vertices.push_back(vertex);
	}

	// 6 indices (two triangles) for this face
	mesh.indices.push_back(baseVertexIndex);     // Triangle 1
	mesh.indices.push_back(baseVertexIndex + 1);
	mesh.indices.push_back(baseVertexIndex + 2);
	mesh.indices.push_back(baseVertexIndex);     // Triangle 2
	mesh.indices.push_back(baseVertexIndex + 2);
	mesh.indices.push_back(baseVertexIndex + 3);
}

void Chunk::setBlock(int x, int y, int z, BlockType type) {
	if (x < 0 || x >= chunkSize || y < 0 || y >= chunkHeight || z < 0 || z >= chunkSize) return;//if outside chunk

	// Only grow here, the next mesh rebuild works out the exact tallest block
	if (type != BlockType::AIR)
		currentTallestBlock = std::max(currentTallestBlock, y);

	blocks[getBlockIndex(x, y, z)] = type;
	fullRebuildNeeded = true;
	modified = true;
}

BlockType Chunk::getBlock(int x, int y, int z) const {
	return blocks[getBlockIndex(x, y, z)];
}

// Helper to get the index in the 1D array from 3D coordinates
size_t Chunk::getBlockIndex(int x, int y, int z) const {
	return (static_cast<size_t>(y) * chunkSize * chunkSize) + (z * chunkSize) + x;
}
