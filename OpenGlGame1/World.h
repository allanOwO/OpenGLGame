#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <future>
#include <string>
#include <glm/glm.hpp>

#include "Chunk.h"
#include "ChunkKey.h"
#include "MeshData.h"
#include "TerrainGenerator.h"
#include "ChunkStorage.h"
#include "DebugStats.h"
#include "ChunkMeshPool.h"

// Result of World::raycast
struct RaycastHit {
	glm::ivec3 block;//solid block that was hit
	glm::ivec3 previous;//empty block the ray came from, always shares a face with block
};

// Owns every chunk: loading, unloading, generation and meshing jobs, and block access in world coordinates
// Only the main thread calls into World, worker threads get copies of what they need
// Needs a GL context for its lifetime (the mesh pool)
class World
{
public:
	static constexpr int unloadMargin = 2;//extra chunks kept past render distance before unloading

	World(int seed, int renderDistance);
	~World();
	World(const World&) = delete;
	World& operator=(const World&) = delete;

	// Call once per frame: starts/finishes chunk generation and meshing around the player, unloads far chunks
	void update(const glm::vec3& playerPosition);

	BlockType getBlock(const glm::ivec3& worldPos) const;//AIR if the chunk isn't loaded
	void setBlock(const glm::ivec3& worldPos, BlockType type);

	// First solid block along the ray within maxDistance, false if nothing was hit
	bool raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) const;

	const std::unordered_map<uint64_t, Chunk>& getChunks() const { return chunks; }
	const TerrainGenerator& getTerrain() const { return terrain; }
	const ChunkMeshPool& getMeshPool() const { return meshPool; }
	int getSeed() const { return terrain.getSeed(); }

	void setRenderDistance(int chunks);//can change while playing, chunks load/unload to match
	int getRenderDistance() const { return renderDistance; }

	// True once the chunk under this world position is loaded, so physics doesn't drop the player through it
	bool isLoadedAt(const glm::vec3& worldPos) const;

	// Every chunk in render distance is generated and meshed, and no jobs are running (benchmark waits for this)
	bool isFullyLoaded() const;
	WorldStats getStats() const;//for the debug overlay and benchmark

	// Saving: writes every modified chunk (loaded or not) to one file
	bool saveChunks(const std::string& path);
	// Loading: call before the first update, chunks then come from the file instead of the noise as they load
	bool loadChunks(const std::string& path);

private:
	Chunk* findChunk(int chunkX, int chunkZ);//chunk coordinates
	const Chunk* findChunk(int chunkX, int chunkZ) const;

	// loading
	void rebuildLoadOrder();//call when render distance changes
	void updateLoading(const glm::ivec3& playerChunkPos);
	void updateActiveChunks();//uses lastPlayerChunk
	bool isInRange(uint64_t key) const;//within render distance of the player's chunk
	void unloadFarChunks(const glm::ivec3& playerChunkPos);
	void generateChunkAsync(int chunkX, int chunkZ);
	void tryApplyChunkGeneration(bool waitForAll = false);
	void markNeighboursForRebuild(int chunkX, int chunkZ);

	// meshing
	void processChunkMeshing(const glm::vec3& playerPosition);
	void launchChunkMesh(uint64_t key, Chunk& chunk);
	void requestMesh(uint64_t key);//chunk's mesh is out of date, rebuild it when a worker is free

	TerrainGenerator terrain;

	// Modified chunks that are currently unloaded, a world is its seed + these + the modified chunks still loaded
	ChunkStorage storage;

	// Every chunk's mesh, chunks only hold their range in it
	ChunkMeshPool meshPool;

	//only touched on the main thread, worker threads get copies (see ChunkMeshInput)
	std::unordered_map<uint64_t, Chunk> chunks;

	// A mesh being built on a worker, editTime is the edit it includes (-1 if it was not started by an edit)
	struct MeshJob {
		std::future<MeshData> future;
		double editTime = -1.0;
	};

	//async tasks for each chunk, keyed by chunk key
	std::unordered_map<uint64_t, std::future<Chunk>> chunkGenerationFutures;
	std::unordered_map<uint64_t, MeshJob> chunkMeshFutures;

	// Timings for the debug overlay, workers add to the first two so they're atomic
	JobTimer generationTimer;
	JobTimer meshTimer;
	JobTimer editLatencyTimer;

	int renderDistance;//in chunks
	bool isInitialLoading = true; // Flag to track initial loading phase

	// Offsets (in chunks) from the player's chunk to every chunk in render distance, nearest first
	// Sorted once when render distance changes, instead of sorting thousands of positions every frame
	std::vector<glm::ivec2> loadOrder;
	// Everything in loadOrder before this index is loaded or generating, so each frame carries on from here
	// Back to 0 whenever the player enters another chunk, once the whole list is done loading costs nothing
	size_t loadCursor = 0;
	glm::ivec3 lastPlayerChunk = glm::ivec3(0);
	bool areaChanged = true;//player chunk or render distance changed since active/unload were last worked out

	// Chunks whose mesh is out of date, so meshing doesn't have to look through every loaded chunk each frame
	std::unordered_set<uint64_t> meshQueue;
};
