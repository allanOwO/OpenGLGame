#pragma once
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

#include "ChunkMeshPool.h"
#include "MeshData.h"
#include "ThreadPool.h"

class TerrainGenerator;

// A 32x32 chunk square of far terrain, drawn as one heightfield mesh
struct FarUnit {
	static constexpr int unitChunks = 32;
	static constexpr int unitBlocks = unitChunks * 16;//512

	glm::ivec2 coords;//in units
	MeshAllocation mesh;
	int cellSize = 0;//blocks per heightfield cell of the current mesh, 0 = none yet
	int wantedCellSize = 0;//what it should be from its distance, rebuilt when they differ
	bool dirty = true;//needs (re)building: new, level changed, or near chunks inside it changed
	double dirtyTime = 0.0;//last time it was marked dirty, rebuilt once that settles
	bool leaving = false;//no longer needed, removed once the near chunks under it are all drawn

	glm::vec3 origin() const;//world position of its corner, what its vertices are relative to
};

// Terrain past detail distance, out to render distance, without loading any chunks there
//
// Close by, the world is real chunks (blocks you can edit, meshed per section). Every loaded chunk costs memory
// and time even when it's so far away it's a few pixels: at render distance 800 that was 2.5 million chunks and 7 GB
// Past detail distance the terrain is drawn straight from the terrain generator instead: one heightfield mesh per
// 32x32 chunk unit, sampled every few blocks, built on a worker. No chunks, no blocks, ~a hundred KB per unit
//
// The two meet without holes: a unit leaves out the cells whose real chunk is loaded *and* fully meshed,
// and draws everything else (so nothing goes missing while chunks stream in and out). Edges of a unit, and of any
// cell it leaves out, get walls down past the lowest ground, the same idea as the LOD seams in Chunk.cpp
//
// Edits far away: when an edited chunk unloads, World remembers its highest block, and the far mesh uses that,
// so a tower you built is still a pillar on the horizon
//
// Main thread only, apart from the jobs it gives the workers
class FarTerrain
{
public:
	// nearShown(chunkX, chunkZ): that chunk is drawn by the near tier, so the far mesh leaves a hole for it
	// editedTop(chunkX, chunkZ): highest block of an edited chunk that isn't loaded (chunk local y), or -1
	FarTerrain(const TerrainGenerator& terrain, ThreadPool& workers, ChunkMeshPool& pool,
		std::function<bool(int, int)> nearShown, std::function<int(int, int)> editedTop);
	~FarTerrain();//waits for running jobs (they read the terrain generator)
	FarTerrain(const FarTerrain&) = delete;
	FarTerrain& operator=(const FarTerrain&) = delete;

	// Once a frame. Units needed come from the distances, builds are started and finished within the budget
	void update(const glm::ivec3& playerChunk, int detailDistance, int renderDistance, double budgetSeconds);
	// A near chunk started or stopped being drawn, the unit around it redoes its hole
	void chunkChanged(int chunkX, int chunkZ);

	const std::unordered_map<uint64_t, FarUnit>& getUnits() const { return units; }
	bool isSettled() const;//nothing waiting to be built (the benchmark waits for this)
	int jobCount() const { return static_cast<int>(jobs.size()); }

private:
	void refreshUnits();//which units are needed and at what cell size, after the player or the distances changed
	bool coveredByNear(const FarUnit& unit) const;//every chunk under it is drawn by the near tier
	void startBuild(uint64_t key, FarUnit& unit);

	const TerrainGenerator& terrain;
	ThreadPool& workers;
	ChunkMeshPool& pool;
	std::function<bool(int, int)> nearShown;
	std::function<int(int, int)> editedTop;

	std::unordered_map<uint64_t, FarUnit> units;
	struct Job {
		std::future<MeshData> future;
		int cellSize;
	};
	std::unordered_map<uint64_t, Job> jobs;

	glm::ivec3 playerChunk = glm::ivec3(0);
	int detailDistance = 0, renderDistance = 0;
	bool needsRefresh = true;
};
