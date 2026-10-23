#pragma once
#include <unordered_map>
#include <future>
#include <glm/glm.hpp>

#include "Chunk.h"
#include "ChunkKey.h"
#include "MeshData.h"
#include "TerrainGenerator.h"
#include "ChunkStorage.h"

// Result of World::raycast
struct RaycastHit {
	glm::ivec3 block;//solid block that was hit
	glm::ivec3 previous;//empty block the ray came from, always shares a face with block
};

// Owns every chunk: loading, unloading, generation and meshing jobs, and block access in world coordinates
// Only the main thread calls into World, worker threads get copies of what they need
class World
{
public:
	static constexpr int renderDistance = 32;//in chunks
	static constexpr int unloadMargin = 2;//extra chunks kept past render distance before unloading

	explicit World(int seed);
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

private:
	Chunk* findChunk(int chunkX, int chunkZ);//chunk coordinates
	const Chunk* findChunk(int chunkX, int chunkZ) const;

	// loading
	void updateLoading(const glm::ivec3& playerChunkPos, const glm::vec3& playerPosition);
	void unloadFarChunks(const glm::ivec3& playerChunkPos);
	void generateChunkAsync(int chunkX, int chunkZ);
	void tryApplyChunkGeneration();
	void markNeighboursForRebuild(int chunkX, int chunkZ);

	// meshing
	void processChunkMeshing(const glm::vec3& playerPosition);
	void launchChunkMesh(uint64_t key, Chunk& chunk);

	TerrainGenerator terrain;

	// Modified chunks that are currently unloaded, a world is its seed + these + the modified chunks still loaded
	ChunkStorage storage;

	//only touched on the main thread, worker threads get copies (see ChunkMeshInput)
	std::unordered_map<uint64_t, Chunk> chunks;

	//async tasks for each chunk, keyed by chunk key
	std::unordered_map<uint64_t, std::future<Chunk>> chunkGenerationFutures;
	std::unordered_map<uint64_t, std::future<MeshData>> chunkMeshFutures;

	bool isInitialLoading = true; // Flag to track initial loading phase
	int currentLoadingRadius = 0; // Current radius for loading chunks, grows to renderDistance
};
