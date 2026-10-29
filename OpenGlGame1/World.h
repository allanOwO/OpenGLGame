#pragma once
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <deque>
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
#include "ThreadPool.h"
#include "Frustum.h"
#include "FarTerrain.h"

// Result of World::raycast
struct RaycastHit {
	glm::ivec3 block;//solid block that was hit
	glm::ivec3 previous;//empty block the ray came from, always shares a face with block
};

// A square of regionSize x regionSize chunk columns, so the renderer can skip a whole group with one frustum test
// Also the unit of merged drawing: everything in it outside simulation distance is copied into one mesh, one draw
// (the GPU pays a fixed cost per draw, so 64 columns of small far meshes as 64+ draws was most of the frame)
struct ChunkRegion {
	static constexpr int regionSize = 8;
	glm::ivec2 coords;//in regions
	std::vector<Chunk*> chunks;//loaded chunks inside it, pointers stay valid because unordered_map never moves its elements
	MeshAllocation merged;//sections in each chunk's mergedSections, copied end to end
	// False once the renderer has checked that every active section in it is in `merged`, so it can skip the
	// region's 64 columns entirely. World sets it again whenever a section might be drawn on its own
	// (at render distance 400 visiting every column of every region was most of the CPU's frame)
	mutable bool mayHaveLoose = true;
	double dirtyTime = 0.0;//last time something in it changed (nowSeconds), it's merged again once that settles

	glm::vec3 origin() const {//world position of its corner, what every vertex in it is relative to
		float width = static_cast<float>(regionSize * Chunk::chunkSize);
		return glm::vec3(coords.x * width, static_cast<float>(-Chunk::baseTerrainHeight), coords.y * width);
	}
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
	void update(const glm::vec3& playerPosition, const LodView& view = LodView());

	BlockType getBlock(const glm::ivec3& worldPos) const;//AIR if the chunk isn't loaded
	void setBlock(const glm::ivec3& worldPos, BlockType type);

	// First solid block along the ray within maxDistance, false if nothing was hit
	bool raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) const;

	const std::unordered_map<uint64_t, Chunk>& getChunks() const { return chunks; }
	const TerrainGenerator& getTerrain() const { return terrain; }
	const ChunkMeshPool& getMeshPool() const { return meshPool; }
	const std::unordered_map<uint64_t, ChunkRegion>& getRegions() const { return regions; }
	const std::unordered_map<uint64_t, FarUnit>& getFarUnits() const { return farTerrain->getUnits(); }
	int getSeed() const { return terrain.getSeed(); }

	void setRenderDistance(int chunks);//can change while playing, chunks load/unload to match
	// Columns closer than this are drawn one section per draw so edits rebuild quickly, the rest are merged per region
	// Nothing simulates yet, it's what water, crops etc. would use later. At or above render distance nothing merges
	void setSimulationDistance(int chunks);
	int getSimulationDistance() const { return simulationDistance; }
	int getRenderDistance() const { return renderDistance; }//how far real chunks are loaded (the detail distance)
	// How far anything is drawn. Past the detail distance it's FarTerrain's heightfield units, not chunks
	void setFarDistance(int chunks) { farDistance = chunks; }
	int getFarDistance() const { return std::max(farDistance, renderDistance); }

	// Where each LOD level starts, as a percentage of render distance: 2x blocks, 4x blocks, 8x blocks
	// 100 means that level is never used
	void setLodDistances(int percent2x, int percent4x, int percent8x);
	int getLodPercent(int level) const { return lodPercent[level]; }//0 = where 2x starts, 1 = 4x, 2 = 8x
	static constexpr float minFullDetailChunks = 4.0f;//never less than this around the player, whatever the settings say

	// True once the chunk under this world position is loaded, so physics doesn't drop the player through it
	bool isLoadedAt(const glm::vec3& worldPos) const;

	// Every chunk in render distance is generated and meshed, and no jobs are running (benchmark waits for this)
	bool isFullyLoaded() const;
	WorldStats getStats() const;//for the debug overlay and benchmark
	const UpdateTimes& getUpdateTimes() const { return updateTimes; }//parts of the last update()

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
	size_t cursorAfterMove(const glm::ivec3& move) const;//where loading can resume after the player changes chunk
	void updateActiveChunks();//uses lastPlayerChunk
	bool isInRange(uint64_t key) const;//within render distance of the player's chunk
	bool isPastUnloadDistance(uint64_t key) const;
	void unloadFarChunks(const glm::ivec3& playerChunkPos);
	// Moving from one chunk to the next only changes which chunks are at the edges of the loaded square,
	// so only those strips are looked at (a few hundred chunks instead of all 160k at render distance 200)
	void updateEdges(const glm::ivec3& from, const glm::ivec3& to);
	std::unordered_map<uint64_t, Chunk>::iterator unloadChunk(std::unordered_map<uint64_t, Chunk>::iterator it);//returns the next one
	void generateChunkAsync(int chunkX, int chunkZ);
	void tryApplyChunkGeneration(bool waitForAll = false);
	void markNeighboursForRebuild(int chunkX, int chunkZ);
	// All 8 neighbours are loaded, or are outside render distance (they won't be, so they count as air)
	// A chunk is only meshed once this is true: its border faces and corner shading depend on them,
	// and meshing before they arrive meant meshing again for each one (up to ~5 times per chunk while loading)
	bool neighboursReady(uint64_t key) const;
	void findNeighbours(uint64_t key, const Chunk* neighbours[8]) const;//order as in Chunk::makeMeshInput

	// meshing
	void processChunkMeshing(const glm::vec3& playerPosition);
	// Starts a mesh job for the section, or clears its mesh straight away if it can't have faces. False if no job was needed
	bool launchSectionMesh(const SectionKey& key, Chunk& chunk);
	void requestMesh(const SectionKey& key);//section's mesh is out of date, rebuild it when a worker is free
	void requestMesh(const SectionKey& key, Chunk& chunk);//same when the chunk is already found
	void requestColumnMesh(uint64_t key);//every section of a column that could have faces

	TerrainGenerator terrain;

	// Modified chunks that are currently unloaded, a world is its seed + these + the modified chunks still loaded
	ChunkStorage storage;

	// Every chunk's mesh, chunks only hold their range in it
	ChunkMeshPool meshPool;

	//only touched on the main thread, worker threads get copies (see ChunkMeshInput)
	std::unordered_map<uint64_t, Chunk> chunks;
	std::unordered_map<uint64_t, ChunkRegion> regions;//same chunks grouped for culling, kept in step with chunks
	void addToRegion(uint64_t key, Chunk& chunk);
	void markRegionLoose(uint64_t chunkKey);//a section in it may now be drawn on its own, see ChunkRegion::mayHaveLoose
	void setActive(uint64_t key, Chunk& chunk, bool active);//keeps activeChunks right

	// Running totals for getStats, adding them up over every chunk took ~150 ms at render distance 400
	size_t blockBytes = 0;
	int activeChunks = 0;
	void removeFromRegion(uint64_t key, Chunk& chunk);

	// merged region meshes
	int simulationDistance = 16;

	// far terrain
	int farDistance = 0;
	std::unique_ptr<FarTerrain> farTerrain;//made in the constructor, after the pool and workers it uses
	// Highest block of each edited chunk that isn't loaded, so far terrain can still show it (FarTerrain)
	std::unordered_map<uint64_t, int16_t> editedTops;
	static int highestBlock(const std::vector<BlockType>& flat);
	bool chunkShown(int chunkX, int chunkZ) const;//loaded, active and every section meshed
	void meshFinished(uint64_t key, Chunk& chunk);//one of its pending meshes is done (or wasn't needed)
	std::unordered_set<uint64_t> dirtyRegions;//region keys whose merged mesh is out of date
	bool mergesColumn(uint64_t key) const;//outside simulation distance, so it belongs in its region's merged mesh
	void markRegionDirty(uint64_t chunkKey);//something in this chunk's region changed
	void sectionMeshChanged(uint64_t key, Chunk& chunk, int section);//marks its region if the merged mesh is affected
	void checkSimulationBoundary();//player moved: regions where a column crossed simulation distance need merging again
	void rebuildRegion(ChunkRegion& region);
	void rebuildDirtyRegions();//the ones that have settled, within a time budget

	// A mesh being built on a worker, editTime is the edit it includes (-1 if it was not started by an edit)
	struct MeshJob {
		std::future<MeshData> future;
		double editTime = -1.0;
		uint8_t lod = 0;//what it's being built at, the column may have moved on by the time it finishes
	};

	// Workers for generation and meshing, made once per world instead of a thread per job
	ThreadPool workers;

	//async tasks for each chunk, keyed by chunk key
	std::unordered_map<uint64_t, std::future<Chunk>> chunkGenerationFutures;
	std::unordered_map<SectionKey, MeshJob, SectionKeyHash> chunkMeshFutures;

	// Timings for the debug overlay, workers add to the first two so they're atomic
	JobTimer generationTimer;
	JobTimer meshTimer;
	JobTimer editLatencyTimer;
	UpdateTimes updateTimes;

	int renderDistance;//in chunks
	bool isInitialLoading = true; // Flag to track initial loading phase
	bool initialLoadComplete = false;//everything in range generated and meshed once, meshing switches to its small per frame budget

	// Offsets (in chunks) from the player's chunk to every chunk in render distance, nearest first
	// Sorted once when render distance changes, instead of sorting thousands of positions every frame
	std::vector<glm::ivec2> loadOrder;
	// Everything in loadOrder before this index is loaded or generating, so each frame carries on from here
	// Back to 0 whenever the player enters another chunk, once the whole list is done loading costs nothing
	size_t loadCursor = 0;
	glm::ivec3 lastPlayerChunk = glm::ivec3(0);
	bool areaChanged = true;//player chunk or render distance changed since active/unload were last worked out

	// Sections whose mesh is out of date, so meshing doesn't have to look through every loaded chunk each frame
	// ChunkSection::queued says what's queued (so nothing is queued twice), meshHeap hands out the nearest first
	// Sorting every queued section each frame cost 8 ms with tens of thousands queued, a heap only touches the few it hands out
	struct QueuedMesh {
		float distanceSquared;//to the player when queued, refreshed when the player changes chunk
		SectionKey key;
		bool operator>(const QueuedMesh& other) const { return distanceSquared > other.distanceSquared; }
	};
	std::priority_queue<QueuedMesh, std::vector<QueuedMesh>, std::greater<QueuedMesh>> meshHeap;//smallest distance on top
	std::vector<SectionKey> parkedMeshes;//queued but outside render distance, back in the heap when the player moves
	glm::vec3 lastPlayerPosition = glm::vec3(0.0f);
	void pushMesh(const SectionKey& key, const Chunk& chunk);

	// level of detail
	int lodPercent[3] = { 15, 30, 60 };//see setLodDistances
	LodView lodView;//from the last update
	bool lodsChanged = true;//settings or render distance changed, every column needs checking
	glm::vec3 lastLodForward = glm::vec3(0.0f);//camera direction when the zoomed view was last checked
	// Regions waiting for their columns' levels to be checked, worked through a little each frame (processLodQueue)
	// Zooming at render distance 200 changes the level of tens of thousands of columns at once,
	// doing them all in one frame was a 80-740 ms freeze
	std::deque<uint64_t> lodQueue;
	std::unordered_set<uint64_t> lodQueued;//what's in lodQueue, so a region is never in it twice
	std::unordered_set<uint64_t> zoomRegions;//regions with a column at more detail because of zoom (Chunk::zoomDetail)
	void queueRegionLods(uint64_t regionKey);
	void processLodQueue();
	float lodStart(int level) const;//distance in chunks where a level starts, infinity if it's switched off
	int lodForDistance(float chunks) const;//level for something this many chunks away, ignoring zoom
	int targetLod(uint64_t key, const Chunk& chunk) const;//level the column should be at right now
	// Each of these re-picks levels for some columns and queues a remesh for the ones that changed (checkLod)
	// Looking at all 160k columns at render distance 200 takes ~20 ms, so only the full one does that
	void checkLod(uint64_t key, Chunk& chunk);
	void updateAllLods();//settings changed: queues every region
	void updateLodRings(float moved);//player moved this many chunks: only columns near a level boundary can change
	void updateZoomedLods();//zoom changed or the view turned: queues the regions in view and the ones with zoom detail
	void refreshMeshHeap();//new distances after the player moved, and gives parked sections another chance
};
