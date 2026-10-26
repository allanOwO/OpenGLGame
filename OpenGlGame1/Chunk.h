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
// The ones the surface passes through are `terrain`: no array either, their blocks are worked out from the
// column's heights (Chunk::heights). Only a section the player edits gets a real array, 4 KB
// Meshing per section means a block edit rebuilds 4096 blocks instead of the whole 65536 block column
struct ChunkSection {
	static constexpr int size = 16;
	static constexpr int volume = size * size * size;

	std::vector<BlockType> blocks;//empty when every block is `fill`, or when it's terrain
	BlockType fill = BlockType::AIR;
	bool terrain = false;//blocks come from the column's heights, see Chunk::terrainBlock

	MeshAllocation mesh;//where its mesh is in World's ChunkMeshPool, World uploads and releases it
	uint8_t meshLod = 0;//detail level the current mesh was built at (see Chunk::lod), for the debug overlay
	// While the section is in its region's merged mesh (Chunk::mergedSections), its vertices live only there
	// (keeping its own copy too doubled mesh memory). This is where, as an offset into the merged mesh
	// The merged mesh is grouped by face direction too, so the slice is one piece per FaceGroup
	// mergedCount 0 = that slice is out of date (the section was remeshed since), `mesh` has the newest
	GLint mergedFirst[faceGroupCount] = {};
	GLsizei mergedGroupCounts[faceGroupCount] = {};
	GLsizei mergedCount = 0;//all groups together
	bool rebuildNeeded = true;
	bool queued = false;//in World's mesh queue, so it's never added twice (a flag here instead of a set of keys:
	                    //at render distance 400 the set rehashed ~650k entries in one frame)
	double pendingEditTime = -1.0;//when the oldest edit not yet in a mesh job happened (nowSeconds), -1 if none

	bool isUniform() const { return blocks.empty() && !terrain; }
	bool isEmpty() const { return isUniform() && fill == BlockType::AIR; }
	static int index(int x, int y, int z) { return (y * size + z) * size + x; }//x innermost, so rows can be memcpy'd

	// Not for terrain sections (they need the column's heights), use Chunk::getBlock / setBlock
	bool set(int x, int y, int z, BlockType type);//false if the block was already that type
	void compress();//drops the array if every block turned out the same
	size_t memoryBytes() const { return blocks.capacity() * sizeof(BlockType); }
};

// Everything meshing a section needs from the 3x3 columns around it, when none of them has edited blocks nearby:
// which sections are sky, rock or terrain, and the terrain heights. About 5 KB of copying on the main thread,
// instead of building the 18x18x18 block copy there (that was most of the main thread's time while loading)
// The worker turns it into the same ChunkMeshInput with Chunk::expand
struct TerrainMeshSource {
	int section = 0;
	// Columns by (dz + 1) * 3 + (dx + 1). Sections by s - section + 1, so 0 is the one below, 2 the one above
	bool loaded[9] = {};
	bool terrain[9][3] = {};
	BlockType fill[9][3] = {};
	std::array<int16_t, 256> heights[9];
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
	// The cheap version for unedited surroundings (most of the world), false if a section nearby has an edit
	bool makeTerrainSource(int section, const Chunk* const neighbours[8], TerrainMeshSource& source) const;
	static ChunkMeshInput expand(const TerrainMeshSource& source);//on the worker, same result makeMeshInput would give
	// True when the section can't have any visible faces: all air, or solid with solid sections all around it
	// (most of the world underground), so it needs no mesh job at all
	bool sectionHasNoFaces(int section, const Chunk* const neighbours[8]) const;
	static MeshData buildMesh(const ChunkMeshInput& input, int lod = 0);//greedy meshing with ambient occlusion
	// One rectangle the same way the mesher makes them (winding, texture coordinates), no ambient occlusion,
	// added to the list for its face group. For other meshers (FarTerrain)
	// axis 0/1/2 = x/y/z it faces along, direction -1/+1, origin on the face's plane,
	// du/dv its full size along the axes after `axis` ((axis + 1) % 3 then (axis + 2) % 3)
	static void appendQuad(MeshData (&groups)[faceGroupCount], const glm::ivec3& origin, const glm::ivec3& du, const glm::ivec3& dv,
		int axis, int direction, BlockType type);

	// Level of detail: far away a block is a few pixels, so drawing every one wastes triangles
	// Level 0 is full detail, each level after doubles the block size: 1 LOD block = 2x2x2, 4x4x4, then 8x8x8 real blocks
	// Only the mesh changes, the real blocks are all still there (physics, saving and editing never see LOD)
	static constexpr int lodLevels = 4;
	static int lodScale(int lod) { return 1 << lod; }
	// Shrinks a full detail mesh input so each cell covers scale^3 blocks, see Chunk.cpp for how cells are picked
	static ChunkMeshInput downsample(const ChunkMeshInput& full, int lod);
	uint8_t lod = 0;//level the whole column is meshed at, World picks it from distance and zoom
	bool zoomDetail = false;//lod is finer than its distance gives, because it was in the zoomed view

	std::array<ChunkSection, sectionCount> sections;

	// Height of the grass block in each x/z column (chunk local y), index z * chunkSize + x
	// An unedited column is just this: grass on top, 3 dirt under it, stone the rest of the way down
	// 512 bytes instead of the ~5 KB of block arrays the surface sections used to take
	std::array<int16_t, chunkSize * chunkSize> heights{};
	static BlockType terrainBlock(int height, int y) {
		if (y > height) return BlockType::AIR;
		if (y == height) return BlockType::GRASS;
		if (y >= height - 3) return BlockType::DIRT;
		return BlockType::STONE;
	}
	glm::vec3 chunkPosition;//world position of the column's bottom corner
	glm::vec3 sectionOrigin(int section) const { return chunkPosition + glm::vec3(0.0f, section * ChunkSection::size, 0.0f); }

	// Bit s set = section s has a mesh to draw. Lets the renderer skip empty sections without touching them,
	// and gives the column's drawn height range for culling. Kept up to date by updateMeshedBit
	uint16_t meshedSections = 0;
	void updateMeshedBit(int section) {
		if (sections[section].mesh.vertexCount > 0) meshedSections |= static_cast<uint16_t>(1u << section);
		else meshedSections &= static_cast<uint16_t>(~(1u << section));
	}

	// Bit s set = section s is drawn as part of its region's merged mesh instead of on its own (see World::rebuildRegion)
	// Sections not in it (inside simulation distance, or meshed since the last merge) are drawn on their own
	uint16_t mergedSections = 0;
	bool mergeWanted = false;//was outside simulation distance when its region was last merged

	bool modified = false;//player changed a block, so it must be kept when unloaded (it can't be regenerated)
	int pendingMeshes = 0;//sections queued or being meshed. 0 (with its neighbours there) = fully drawn, see World::chunkShown

	size_t memoryBytes() const {//block storage, for World's running total
		size_t bytes = sizeof(heights);
		for (const ChunkSection& section : sections) bytes += section.memoryBytes();
		return bytes;
	}

private:
	void buildSections(const std::vector<BlockType>& flat);//splits a whole column array into compressed sections
};
