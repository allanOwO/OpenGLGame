#include "Chunk.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <cstddef>//offsetof

#include "TerrainGenerator.h"
#include "BlockConstants.h"



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
	//IPointer keeps them as integers in the shader (no conversion to float), locations match vertex_shader.glsl
	glVertexAttribIPointer(0, 3, GL_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, pos));
	glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, normal));
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, ao));
	glVertexAttribIPointer(3, 2, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, uv));
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, tile));
	for (int i = 0; i <= 4; i++) {
		glEnableVertexAttribArray(i);
	}

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

ChunkMeshInput Chunk::makeMeshInput(const Chunk* const neighbours[8]) const {
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
		// Single corner column from each diagonal neighbour
		const int last = chunkSize - 1;
		if (neighbours[4]) set(chunkSize, y, chunkSize, neighbours[4]->getBlock(0, y, 0));    // +X+Z
		if (neighbours[5]) set(chunkSize, y, -1, neighbours[5]->getBlock(0, y, last));        // +X-Z
		if (neighbours[6]) set(-1, y, chunkSize, neighbours[6]->getBlock(last, y, 0));        // -X+Z
		if (neighbours[7]) set(-1, y, -1, neighbours[7]->getBlock(last, y, last));            // -X-Z
	}
	return input;
}

// ---------- greedy meshing ----------

// Face index (see BlockFace) from the axis a face points along (0 = x, 1 = y, 2 = z) and which way (-1 or +1)
static int faceIndex(int axis, int direction) {
	switch (axis) {
	case 0: return direction < 0 ? 2 : 3; // Left, Right
	case 1: return direction < 0 ? 5 : 4; // Bottom, Top
	default: return direction < 0 ? 0 : 1; // Front, Back
	}
}

static bool isSolid(const ChunkMeshInput& input, const glm::ivec3& p) {
	if (p.y < 0 || p.y >= Chunk::chunkHeight) return false;
	return input.get(p.x, p.y, p.z) != BlockType::AIR;
}

// Ambient occlusion for one corner of a face: 3 = open, 0 = fully tucked into a corner
// side1/side2 are the blocks along each edge next to the corner, corner is the one diagonally across
static uint32_t vertexAO(bool side1, bool side2, bool corner) {
	if (side1 && side2) return 0;// both edges blocked, the diagonal can't let any more light in
	return 3 - (side1 + side2 + corner);
}

// Texture coordinates in blocks for a vertex of a face
// Picked so every face's texture is upright (grass sides have grass at the top) and repeats once per block
// Offsets like chunkHeight - y flip the direction while keeping the numbers positive for uint16
static glm::ivec2 faceUV(int face, const glm::ivec3& p) {
	switch (face) {
	case 0:  return { p.x, Chunk::chunkHeight - p.y };                   // Front (-Z)
	case 1:  return { Chunk::chunkSize - p.x, Chunk::chunkHeight - p.y }; // Back (+Z)
	case 2:  return { Chunk::chunkSize - p.z, Chunk::chunkHeight - p.y }; // Left (-X)
	case 3:  return { p.z, Chunk::chunkHeight - p.y };                   // Right (+X)
	case 4:  return { p.x, Chunk::chunkSize - p.z };                     // Top
	default: return { p.x, p.z };                                        // Bottom
	}
}

// Mask cell layout: bit 16 = face present, bits 8-15 = atlas tile, bits 0-7 = AO for the 4 corners (2 bits each)
// Two cells can merge only if the whole value matches, so same texture AND same corner shading
static uint32_t makeCell(uint32_t tile, uint32_t ao00, uint32_t ao10, uint32_t ao11, uint32_t ao01) {
	return (1u << 16) | (tile << 8) | ao00 | (ao10 << 2) | (ao11 << 4) | (ao01 << 6);
}

// Adds one rectangle covering several blocks
// origin is its corner on the face plane, du/dv are its full width and height along the u/v axes
static void addQuad(MeshData& mesh, const glm::ivec3& origin, const glm::ivec3& du, const glm::ivec3& dv,
	int direction, int face, uint32_t cell) {

	uint16_t tile = static_cast<uint16_t>((cell >> 8) & 0xFF);
	uint8_t ao[4] = {// corners in order (-u,-v) (+u,-v) (+u,+v) (-u,+v)
		static_cast<uint8_t>(cell & 3), static_cast<uint8_t>((cell >> 2) & 3),
		static_cast<uint8_t>((cell >> 4) & 3), static_cast<uint8_t>((cell >> 6) & 3)
	};
	glm::ivec3 corners[4] = { origin, origin + du, origin + du + dv, origin + dv };

	// cross(u, v) points along +axis, so going round the corners in order is counter clockwise (front facing)
	// seen from the + side; faces pointing the - way go round the other way
	const int forward[4] = { 0, 1, 2, 3 };
	const int backward[4] = { 0, 3, 2, 1 };
	const int* order = direction > 0 ? forward : backward;

	unsigned int base = static_cast<unsigned int>(mesh.vertices.size());
	for (int i = 0; i < 4; i++) {
		int c = order[i];
		glm::ivec2 uv = faceUV(face, corners[c]);
		PackedVertex vertex{};
		vertex.pos[0] = static_cast<int16_t>(corners[c].x);
		vertex.pos[1] = static_cast<int16_t>(corners[c].y);
		vertex.pos[2] = static_cast<int16_t>(corners[c].z);
		vertex.normal = static_cast<uint8_t>(face);
		vertex.ao = ao[c];
		vertex.uv[0] = static_cast<uint16_t>(uv.x);
		vertex.uv[1] = static_cast<uint16_t>(uv.y);
		vertex.tile = tile;
		mesh.vertices.push_back(vertex);
	}

	// A quad is 2 triangles and either diagonal works, but the GPU blends AO across each triangle
	// Split along the brighter diagonal so one dark corner doesn't smear across the whole quad
	if (ao[0] + ao[2] < ao[1] + ao[3]) {
		unsigned int indices[6] = { base, base + 1, base + 3, base + 1, base + 2, base + 3 };
		mesh.indices.insert(mesh.indices.end(), indices, indices + 6);
	}
	else {
		unsigned int indices[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
		mesh.indices.insert(mesh.indices.end(), indices, indices + 6);
	}
}

MeshData Chunk::buildMesh(const ChunkMeshInput& input) {
	MeshData mesh;
	mesh.tallestBlock = std::max(input.maxY, 0);
	if (input.maxY < 0) return mesh;//empty chunk

	// Area to mesh: the whole chunk on x/z, y only up to the tallest block
	const int size[3] = { chunkSize, input.maxY + 1, chunkSize };
	std::vector<uint32_t> mask;

	for (int axis = 0; axis < 3; axis++) {
		// u and v are the two directions across a face, chosen so cross(u, v) points along +axis
		int u = (axis + 1) % 3;
		int v = (axis + 2) % 3;
		glm::ivec3 uStep(0), vStep(0);
		uStep[u] = 1;
		vStep[v] = 1;
		mask.assign(static_cast<size_t>(size[u]) * size[v], 0);

		for (int direction = -1; direction <= 1; direction += 2) {
			int face = faceIndex(axis, direction);

			// One 2D slice of the chunk at a time
			for (int slice = 0; slice < size[axis]; slice++) {

				// 1. Build the mask: what face (if any) each cell of this slice shows
				for (int b = 0; b < size[v]; b++) {
					for (int a = 0; a < size[u]; a++) {
						uint32_t& cell = mask[a + b * size[u]];
						cell = 0;

						glm::ivec3 pos(0);
						pos[axis] = slice;
						pos[u] = a;
						pos[v] = b;
						BlockType type = input.get(pos.x, pos.y, pos.z);
						if (type == BlockType::AIR) continue;

						glm::ivec3 front = pos;//the block the face looks into
						front[axis] += direction;
						if (isSolid(input, front)) continue;//face is hidden

						// Blocks around the face's 4 corners, checked in the layer the face looks into
						bool uBehind = isSolid(input, front - uStep), uAhead = isSolid(input, front + uStep);
						bool vBehind = isSolid(input, front - vStep), vAhead = isSolid(input, front + vStep);
						uint32_t ao00 = vertexAO(uBehind, vBehind, isSolid(input, front - uStep - vStep));
						uint32_t ao10 = vertexAO(uAhead, vBehind, isSolid(input, front + uStep - vStep));
						uint32_t ao11 = vertexAO(uAhead, vAhead, isSolid(input, front + uStep + vStep));
						uint32_t ao01 = vertexAO(uBehind, vAhead, isSolid(input, front - uStep + vStep));

						uint32_t tile = static_cast<uint32_t>(getBlockTile(type, static_cast<BlockFace>(face)));
						cell = makeCell(tile, ao00, ao10, ao11, ao01);
					}
				}

				// 2. Greedy merge: grow each unclaimed cell into the biggest rectangle of identical cells
				for (int b = 0; b < size[v]; b++) {
					for (int a = 0; a < size[u];) {
						uint32_t cell = mask[a + b * size[u]];
						if (cell == 0) { a++; continue; }

						// Grow along u while the cells match
						int width = 1;
						while (a + width < size[u] && mask[a + width + b * size[u]] == cell) {
							width++;
						}

						// Grow along v while every cell in the next row matches
						int height = 1;
						bool rowMatches = true;
						while (b + height < size[v] && rowMatches) {
							for (int k = 0; k < width; k++) {
								if (mask[a + k + (b + height) * size[u]] != cell) { rowMatches = false; break; }
							}
							if (rowMatches) height++;
						}

						// Faces pointing + sit on the far side of the block
						glm::ivec3 origin(0);
						origin[axis] = slice + (direction > 0 ? 1 : 0);
						origin[u] = a;
						origin[v] = b;
						addQuad(mesh, origin, uStep * width, vStep * height, direction, face, cell);

						// Claim the cells so they aren't used again
						for (int h = 0; h < height; h++) {
							for (int k = 0; k < width; k++) {
								mask[a + k + (b + h) * size[u]] = 0;
							}
						}
						a += width;
					}
				}
			}
		}
	}
	return mesh;
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
