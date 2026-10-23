#include "World.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <limits>
#include <thread>
#include <cmath>

//worker thread count for meshing
static size_t maxAsyncTasks() {
    return std::max(1u, std::thread::hardware_concurrency());
}

static int floorDiv(int a, int b) {
    return static_cast<int>(std::floor(static_cast<float>(a) / b));
}

World::World(int seed) : terrain(seed) {
}

World::~World() {
    // Futures from std::async wait for their thread when destroyed
    // Clear them first so no worker is still using terrain when it's destroyed
    chunkGenerationFutures.clear();
    chunkMeshFutures.clear();
}

void World::update(const glm::vec3& playerPosition) {
    // Calculate the player's chunk position in chunk coordinates
    glm::ivec3 playerChunkPos(
        static_cast<int>(floor(playerPosition.x / Chunk::chunkSize)),
        0, // Y is fixed at 0 for chunk coordinates
        static_cast<int>(floor(playerPosition.z / Chunk::chunkSize))
    );

    updateLoading(playerChunkPos, playerPosition);
    tryApplyChunkGeneration();
    unloadFarChunks(playerChunkPos);
    processChunkMeshing(playerPosition);
}

// ---------- loading ----------

void World::updateLoading(const glm::ivec3& playerChunkPos, const glm::vec3& playerPosition) {
    // Store chunk positions and distances
    std::vector<std::pair<uint64_t, float>> chunkPositions;
    int effectiveRenderDistance = isInitialLoading ? currentLoadingRadius : renderDistance;
    chunkPositions.reserve((2 * effectiveRenderDistance + 1) * (2 * effectiveRenderDistance + 1)); // Preallocate

    for (int x = -effectiveRenderDistance; x <= effectiveRenderDistance; x++) {
        for (int z = -effectiveRenderDistance; z <= effectiveRenderDistance; z++) {
            glm::ivec3 chunkPos = playerChunkPos + glm::ivec3(x, 0, z); // Offset in chunk coordinates
            uint64_t key = getChunkKey(chunkPos.x, chunkPos.z);

            // Use Manhattan distance for faster sorting (no square root needed)
            float distance = std::abs(chunkPos.x * Chunk::chunkSize - playerPosition.x) + std::abs(chunkPos.z * Chunk::chunkSize - playerPosition.z);
            chunkPositions.emplace_back(key, distance);
        }
    }

    // Sort by distance
    std::sort(chunkPositions.begin(), chunkPositions.end(),
        [](const std::pair<uint64_t, float>& a, const std::pair<uint64_t, float>& b) {
            return a.second < b.second;
        });

    std::unordered_set<uint64_t> loadedChunks;
    loadedChunks.reserve(chunkPositions.size());//reduce rehashing
    int chunksGeneratedThisFrame = 0;
    const int maxChunksGeneratedPerFrame = isInitialLoading ? 2 : 1; // Load 2 chunks per frame during initial loading

    for (const auto& pair : chunkPositions) {
        loadedChunks.insert(pair.first);
        if (chunksGeneratedThisFrame >= maxChunksGeneratedPerFrame) continue;

        if (chunks.find(pair.first) == chunks.end() && chunkGenerationFutures.find(pair.first) == chunkGenerationFutures.end()) {
            generateChunkAsync(chunkKeyX(pair.first), chunkKeyZ(pair.first));
            chunksGeneratedThisFrame++;
        }
    }

    // Update the loading radius
    if (isInitialLoading) {
        // Check if all chunks within the current radius are loaded
        int expectedChunks = (2 * currentLoadingRadius + 1) * (2 * currentLoadingRadius + 1);
        int loadedChunksCount = 0;
        for (const auto& pair : chunks) {
            int dx = std::abs(chunkKeyX(pair.first) - playerChunkPos.x);
            int dz = std::abs(chunkKeyZ(pair.first) - playerChunkPos.z);
            if (dx <= currentLoadingRadius && dz <= currentLoadingRadius) {
                loadedChunksCount++;
            }
        }
        if (loadedChunksCount >= expectedChunks) {
            currentLoadingRadius++;
            if (currentLoadingRadius > renderDistance) {
                isInitialLoading = false;
                std::cout << "Initial loading complete! All chunks within render distance loaded." << std::endl;
            }
        }
    }

    // Mark chunks as active/inactive
    for (auto& pair : chunks) {
        pair.second.isActive = loadedChunks.find(pair.first) != loadedChunks.end();
    }
}

void World::generateChunkAsync(int chunkX, int chunkZ) {
    // Launch async task to generate the chunk, the chunk is only added to the map on the main thread
    uint64_t key = getChunkKey(chunkX, chunkZ);
    glm::ivec3 position(chunkX * Chunk::chunkSize, -Chunk::baseTerrainHeight, chunkZ * Chunk::chunkSize);
    const TerrainGenerator* terrainPtr = &terrain;//worker only reads it

    // If the player changed this chunk before, rebuild it from the stored blocks instead of the noise
    std::vector<uint8_t> savedData;
    storage.take(key, savedData);//leaves savedData empty if there's nothing stored

    chunkGenerationFutures[key] = std::async(std::launch::async, [terrainPtr, position, savedData = std::move(savedData)]() {
        if (!savedData.empty()) {
            std::vector<BlockType> blocks;
            if (decodeBlocks(savedData, static_cast<size_t>(Chunk::chunkSize) * Chunk::chunkHeight * Chunk::chunkSize, blocks)) {
                return Chunk(position, std::move(blocks));
            }
            std::cerr << "Stored chunk at " << position.x << ", " << position.z << " is corrupt, regenerating it" << std::endl;
        }
        return Chunk(position, *terrainPtr);
        });
}

void World::tryApplyChunkGeneration() {
    for (auto it = chunkGenerationFutures.begin(); it != chunkGenerationFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

            uint64_t key = it->first; // The key is the uint64_t chunk identifier
            auto inserted = chunks.emplace(key, it->second.get());
            Chunk& chunk = inserted.first->second;

            // Initialize buffers for rendering
            chunk.initializeBuffers();

            // Neighbours meshed without this chunk have faces on the shared border, rebuild them
            markNeighboursForRebuild(chunkKeyX(key), chunkKeyZ(key));

            // Remove the completed future
            it = chunkGenerationFutures.erase(it);
        }
        else {
            ++it;
        }
    }
}

void World::markNeighboursForRebuild(int chunkX, int chunkZ) {
    const glm::ivec2 offsets[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    for (const auto& offset : offsets) {
        if (Chunk* neighbour = findChunk(chunkX + offset.x, chunkZ + offset.y)) {
            neighbour->fullRebuildNeeded = true;
        }
    }
}

void World::unloadFarChunks(const glm::ivec3& playerChunkPos) {
    // Margin stops chunks on the edge being unloaded and reloaded when walking back and forth
    const int unloadDistance = renderDistance + unloadMargin;
    for (auto it = chunks.begin(); it != chunks.end();) {
        int dx = std::abs(chunkKeyX(it->first) - playerChunkPos.x);
        int dz = std::abs(chunkKeyZ(it->first) - playerChunkPos.z);
        if (dx > unloadDistance || dz > unloadDistance) {
            // Unmodified chunks can be regenerated from the seed, only player edits need keeping
            if (it->second.modified) {
                storage.store(it->first, encodeBlocks(it->second.blocks));
            }
            // Any mesh job still running for it only has a copy, its result is dropped when it finishes
            it = chunks.erase(it);
        }
        else {
            ++it;
        }
    }
}

// ---------- meshing ----------

// Copy the chunk and its neighbour borders, then build the mesh on a worker thread
void World::launchChunkMesh(uint64_t key, Chunk& chunk) {
    int chunkX = chunkKeyX(key);
    int chunkZ = chunkKeyZ(key);
    const Chunk* neighbours[4] = {
        findChunk(chunkX + 1, chunkZ), // +X
        findChunk(chunkX - 1, chunkZ), // -X
        findChunk(chunkX, chunkZ + 1), // +Z
        findChunk(chunkX, chunkZ - 1)  // -Z
    };

    ChunkMeshInput input = chunk.makeMeshInput(neighbours);
    // Cleared before the job runs, so edits made while it runs queue another rebuild
    chunk.fullRebuildNeeded = false;

    chunkMeshFutures[key] = std::async(std::launch::async, [input = std::move(input)]() {
        return Chunk::buildMesh(input);
        });
}

void World::processChunkMeshing(const glm::vec3& playerPosition) {
    // Upload finished meshes
    for (auto it = chunkMeshFutures.begin(); it != chunkMeshFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        MeshData mesh = it->second.get();
        if (Chunk* chunk = findChunk(chunkKeyX(it->first), chunkKeyZ(it->first))) {
            chunk->uploadMesh(mesh);
        }// else the chunk was unloaded while meshing
        it = chunkMeshFutures.erase(it);
    }

    if (chunkMeshFutures.size() >= maxAsyncTasks()) return;
    size_t freeSlots = maxAsyncTasks() - chunkMeshFutures.size();

    // Chunks waiting for a mesh, closest to the player first
    std::vector<std::pair<float, uint64_t>> waiting;
    for (auto& pair : chunks) {
        const Chunk& chunk = pair.second;
        if (!chunk.isActive || !chunk.fullRebuildNeeded) continue;
        if (chunkMeshFutures.find(pair.first) != chunkMeshFutures.end()) continue;//one job per chunk at a time

        float dx = chunk.chunkPosition.x + Chunk::chunkSize * 0.5f - playerPosition.x;
        float dz = chunk.chunkPosition.z + Chunk::chunkSize * 0.5f - playerPosition.z;
        waiting.emplace_back(dx * dx + dz * dz, pair.first);
    }

    size_t launchCount = std::min(freeSlots, waiting.size());
    std::partial_sort(waiting.begin(), waiting.begin() + launchCount, waiting.end());
    for (size_t i = 0; i < launchCount; i++) {
        uint64_t key = waiting[i].second;
        launchChunkMesh(key, chunks.at(key));
    }
}

// ---------- block access ----------

Chunk* World::findChunk(int chunkX, int chunkZ) {
    auto it = chunks.find(getChunkKey(chunkX, chunkZ));
    return (it != chunks.end()) ? &it->second : nullptr;
}

const Chunk* World::findChunk(int chunkX, int chunkZ) const {
    auto it = chunks.find(getChunkKey(chunkX, chunkZ));
    return (it != chunks.end()) ? &it->second : nullptr;
}

BlockType World::getBlock(const glm::ivec3& worldPos) const {
    int localY = worldPos.y + Chunk::baseTerrainHeight;
    if (localY < 0 || localY >= Chunk::chunkHeight) return BlockType::AIR;

    int chunkX = floorDiv(worldPos.x, Chunk::chunkSize);
    int chunkZ = floorDiv(worldPos.z, Chunk::chunkSize);
    const Chunk* chunk = findChunk(chunkX, chunkZ);
    if (!chunk) return BlockType::AIR;

    return chunk->getBlock(worldPos.x - chunkX * Chunk::chunkSize, localY, worldPos.z - chunkZ * Chunk::chunkSize);
}

void World::setBlock(const glm::ivec3& worldPos, BlockType type) {
    int localY = worldPos.y + Chunk::baseTerrainHeight;
    if (localY < 0 || localY >= Chunk::chunkHeight) return;

    int chunkX = floorDiv(worldPos.x, Chunk::chunkSize);
    int chunkZ = floorDiv(worldPos.z, Chunk::chunkSize);
    Chunk* chunk = findChunk(chunkX, chunkZ);
    if (!chunk) return;

    int localX = worldPos.x - chunkX * Chunk::chunkSize;
    int localZ = worldPos.z - chunkZ * Chunk::chunkSize;
    chunk->setBlock(localX, localY, localZ, type);

    // Blocks on the edge change the neighbour's visible faces too
    auto rebuild = [this](int x, int z) {
        if (Chunk* neighbour = findChunk(x, z)) neighbour->fullRebuildNeeded = true;
    };
    if (localX == 0) rebuild(chunkX - 1, chunkZ);
    if (localX == Chunk::chunkSize - 1) rebuild(chunkX + 1, chunkZ);
    if (localZ == 0) rebuild(chunkX, chunkZ - 1);
    if (localZ == Chunk::chunkSize - 1) rebuild(chunkX, chunkZ + 1);
}

bool World::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) const {
    // Voxel traversal (Amanatides & Woo), visits every block the ray passes through in order
    glm::vec3 rayDir = glm::normalize(direction);

    glm::ivec3 block = glm::ivec3(glm::floor(origin));
    glm::ivec3 step(0);
    glm::vec3 tMax(std::numeric_limits<float>::infinity());//ray distance to the next block boundary on each axis
    glm::vec3 tDelta(std::numeric_limits<float>::infinity());//ray distance to cross one whole block on each axis

    for (int i = 0; i < 3; i++) {
        if (rayDir[i] > 0.0f) {
            step[i] = 1;
            tMax[i] = (block[i] + 1.0f - origin[i]) / rayDir[i];
            tDelta[i] = 1.0f / rayDir[i];
        }
        else if (rayDir[i] < 0.0f) {
            step[i] = -1;
            tMax[i] = (origin[i] - block[i]) / -rayDir[i];
            tDelta[i] = -1.0f / rayDir[i];
        }
    }

    glm::ivec3 previous = block;
    float t = 0.0f;
    while (t <= maxDistance) {
        if (getBlock(block) != BlockType::AIR) {
            hit.block = block;
            hit.previous = previous;
            return true;
        }

        // Step along whichever axis reaches its next boundary first
        int axis = (tMax.x < tMax.y) ? ((tMax.x < tMax.z) ? 0 : 2) : ((tMax.y < tMax.z) ? 1 : 2);
        previous = block;
        t = tMax[axis];
        block[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
    return false;
}
