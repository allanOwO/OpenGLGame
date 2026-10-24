#pragma once
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "BlockType.h"
#include "MeshData.h"
#include "ChunkMeshPool.h"

class TerrainGenerator;//forward declaration, Chunk only needs a reference

// A 16x16x16 piece of a chunk column, the unit that is stored, meshed and drawn
//
// Most sections are entirely one block: open sky is all air, deep underground is all stone.
// Those keep no array at all, just `fill`, so a column costs a few KB instead of 64KB
// Meshing per section means a block edit rebuilds 4096 blocks instead of the whole 65536 block column
struct ChunkSection {
	static constexpr int size = 16;
	static constexpr int volume = size * size * size;

	std::vector<BlockType> blocks;//empty when every block is `fill`
	BlockType fill = BlockType::AIR;

	MeshAllocation mesh;//where its mesh is in World's ChunkMeshPool, World uploads and releases it
	bool rebuildNeeded = true;
	double pendingEditTime = -1.0;//when the oldest edit not yet in a mesh job happened (nowSeconds), -1 if none

	bool isUniform() const { return blocks.empty(); }
	bool isEmpty() const { return isUniform() && fill == BlockType::AIR; }
	static int index(int x, int y, int z) { return (y * size + z) * size + x; }//x innermost, so rows can be memcpy'd

	BlockType get(int x, int y, int z) const { return blocks.empty() ? fill : blocks[index(x, y, z)]; }
	bool set(int x, int y, int z, BlockType type);//false if the block was already that type
	void compress();//drops the array if every block turned out the same
	size_t memoryBytes() const { return blocks.capacity() * sizeof(BlockType); }
};

// A full height column of sections
class Chunk
{
public:
	static constexpr int chunkSize = 16;
	static constexpr int chunkHeight = 256;
	static constexpr int baseTerrainHeight = 64;
	static constexpr int sectionCount = chunkHeight / ChunkSection::size;
	bool isActive = true;

	Chunk(glm::ivec3 position, const TerrainGenerator& terrain);//constructor, generates terrain (safe to run on a worker thread)
	Chunk(glm::ivec3 position, const std::vector<BlockType>& savedBlocks);//restores a chunk the player modified earlier
	// Copying blocks by accident would be slow, moving is fine (meshes are just offsets into ChunkMeshPool)
	Chunk(const Chunk&) = delete;
	Chunk& operator=(const Chunk&) = delete;
	Chunk(Chunk&&) noexcept = default;
	Chunk& operator=(Chunk&&) noexcept = default;

	bool setBlock(int x, int y, int z, BlockType type);//false if nothing changed
	BlockType getBlock(int x, int y, int z) const;

	// Whole column as one array, y slowest then z then x (the save format, see ChunkStorage)
	std::vector<BlockType> flatBlocks() const;
	static size_t flatIndex(int x, int y, int z) { return (static_cast<size_t>(y) * chunkSize + z) * chunkSize + x; }

	//neighbours are +X, -X, +Z, -Z, then the diagonals +X+Z, +X-Z, -X+Z, -X-Z (ambient occlusion needs the corners)
	//nullptr if not loaded (treated as air)

	// Copies one section + a 1 block border from around it, so its mesh can be built on another thread
	ChunkMeshInput makeMeshInput(int section, const Chunk* const neighbours[8]) const;
	// True when the section can't have any visible faces: all air, or solid with solid sections all around it
	// (most of the world underground), so it needs no mesh job at all
	bool sectionHasNoFaces(int section, const Chunk* const neighbours[8]) const;
	static MeshData buildMesh(const ChunkMeshInput& input);//greedy meshing with ambient occlusion

	std::array<ChunkSection, sectionCount> sections;
	glm::vec3 chunkPosition;//world position of the column's bottom corner
	glm::vec3 sectionOrigin(int section) const { return chunkPosition + glm::vec3(0.0f, section * ChunkSection::size, 0.0f); }

	// Bit s set = section s has a mesh to draw. Lets the renderer skip empty sections without touching them,
	// and gives the column's drawn height range for culling. Kept up to date by updateMeshedBit
	uint16_t meshedSections = 0;
	void updateMeshedBit(int section) {
		if (sections[section].mesh.vertexCount > 0) meshedSections |= static_cast<uint16_t>(1u << section);
		else meshedSections &= static_cast<uint16_t>(~(1u << section));
	}

	bool modified = false;//player changed a block, so it must be kept when unloaded (it can't be regenerated)

private:
	void buildSections(const std::vector<BlockType>& flat);//splits a whole column array into compressed sections
};
