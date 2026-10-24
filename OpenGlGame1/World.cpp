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

World::World(int seed, int renderDistance) : terrain(seed), renderDistance(renderDistance) {
    rebuildLoadOrder();
}

void World::setRenderDistance(int chunks) {
    if (chunks == renderDistance) return;
    renderDistance = chunks;
    rebuildLoadOrder();
}

bool World::isLoadedAt(const glm::vec3& worldPos) const {
    int chunkX = static_cast<int>(std::floor(worldPos.x / Chunk::chunkSize));
    int chunkZ = static_cast<int>(std::floor(worldPos.z / Chunk::chunkSize));
    return findChunk(chunkX, chunkZ) != nullptr;
}

bool World::isFullyLoaded() const {
    if (isInitialLoading || loadCursor < loadOrder.size()) return false;
    if (!chunkGenerationFutures.empty() || !chunkMeshFutures.empty()) return false;
    for (uint64_t key : meshQueue) {
        const Chunk* chunk = findChunk(chunkKeyX(key), chunkKeyZ(key));
        if (chunk && chunk->isActive && chunk->fullRebuildNeeded) return false;
    }
    return true;
}

WorldStats World::getStats() const {
    WorldStats stats;
    stats.chunksLoaded = static_cast<int>(chunks.size());
    stats.generationJobs = static_cast<int>(chunkGenerationFutures.size());
    stats.meshJobs = static_cast<int>(chunkMeshFutures.size());
    stats.generation = JobTotals::of(generationTimer);
    stats.meshing = JobTotals::of(meshTimer);
    stats.editLatency = JobTotals::of(editLatencyTimer);
    for (const auto& pair : chunks) {
        const Chunk& chunk = pair.second;
        if (chunk.isActive) stats.chunksActive++;
        stats.blockBytes += chunk.blocks.capacity() * sizeof(BlockType);
    }
    stats.meshBytes = meshPool.usedBytes();
    return stats;
}

// ---------- saving ----------

bool World::saveChunks(const std::string& path) {
    // A chunk being restored from storage is in neither storage nor chunks until its job finishes
    // Finish those first or their edits would be missing from the save
    tryApplyChunkGeneration(true);

    // Unloaded edited chunks are already encoded in storage, add the edited chunks that are still loaded
    // Done on a copy, storage only ever holds chunks that aren't loaded
    ChunkStorage snapshot = storage;
    for (const auto& pair : chunks) {
        if (pair.second.modified) {
            snapshot.store(pair.first, encodeBlocks(pair.second.blocks));
        }
    }
    return snapshot.saveToFile(path);
}

bool World::loadChunks(const std::string& path) {
    return storage.loadFromFile(path);
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

    // Chunks only enter or leave render distance when the player crosses into another chunk,
    // so the work that looks at every chunk only runs then, not every frame
    if (areaChanged || playerChunkPos != lastPlayerChunk) {
        lastPlayerChunk = playerChunkPos;
        areaChanged = false;
        loadCursor = 0;//new chunks may be missing anywhere in range now
        updateActiveChunks();
        unloadFarChunks(playerChunkPos);
    }

    tryApplyChunkGeneration();//first, so its finished jobs free up workers for updateLoading
    updateLoading(playerChunkPos);
    processChunkMeshing(playerPosition);
}

// ---------- loading ----------

void World::rebuildLoadOrder() {
    loadOrder.clear();
    loadOrder.reserve(static_cast<size_t>(2 * renderDistance + 1) * (2 * renderDistance + 1));
    for (int x = -renderDistance; x <= renderDistance; x++) {
        for (int z = -renderDistance; z <= renderDistance; z++) {
            loadOrder.emplace_back(x, z);
        }
    }
    // Nearest first by real (circular) distance, ties broken by position so the order is the same every run
    std::sort(loadOrder.begin(), loadOrder.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
        int da = a.x * a.x + a.y * a.y, db = b.x * b.x + b.y * b.y;
        if (da != db) return da < db;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
    loadCursor = 0;
    areaChanged = true;
}

void World::updateLoading(const glm::ivec3& playerChunkPos) {
    // Start as many generation jobs as there are free workers, nearest missing chunks first
    // (a fixed number per frame made loading speed depend on the frame rate instead of the CPU)
    size_t running = chunkGenerationFutures.size();
    while (loadCursor < loadOrder.size() && running < maxAsyncTasks()) {
        int chunkX = playerChunkPos.x + loadOrder[loadCursor].x;
        int chunkZ = playerChunkPos.z + loadOrder[loadCursor].y;
        uint64_t key = getChunkKey(chunkX, chunkZ);
        if (chunks.find(key) == chunks.end() && chunkGenerationFutures.find(key) == chunkGenerationFutures.end()) {
            generateChunkAsync(chunkX, chunkZ);
            running++;
        }
        loadCursor++;
    }

    if (isInitialLoading && loadCursor == loadOrder.size() && chunkGenerationFutures.empty()) {
        isInitialLoading = false;
        std::cout << "Initial loading complete! All chunks within render distance loaded." << std::endl;
    }
}

bool World::isInRange(uint64_t key) const {
    return std::abs(chunkKeyX(key) - lastPlayerChunk.x) <= renderDistance
        && std::abs(chunkKeyZ(key) - lastPlayerChunk.z) <= renderDistance;
}

void World::updateActiveChunks() {
    // Active = inside render distance, drawn and meshed. Inactive chunks wait in the unload margin
    for (auto& pair : chunks) {
        pair.second.isActive = isInRange(pair.first);
    }
}

void World::generateChunkAsync(int chunkX, int chunkZ) {
    // Launch async task to generate the chunk, the chunk is only added to the map on the main thread
    uint64_t key = getChunkKey(chunkX, chunkZ);
    glm::ivec3 position(chunkX * Chunk::chunkSize, -Chunk::baseTerrainHeight, chunkZ * Chunk::chunkSize);
    const TerrainGenerator* terrainPtr = &terrain;//worker only reads it
    JobTimer* timer = &generationTimer;//atomic, and World waits for every job before it's destroyed

    // If the player changed this chunk before, rebuild it from the stored blocks instead of the noise
    std::vector<uint8_t> savedData;
    storage.take(key, savedData);//leaves savedData empty if there's nothing stored

    chunkGenerationFutures[key] = std::async(std::launch::async, [terrainPtr, timer, position, savedData = std::move(savedData)]() {
        double start = nowSeconds();
        if (!savedData.empty()) {
            std::vector<BlockType> blocks;
            if (decodeBlocks(savedData, static_cast<size_t>(Chunk::chunkSize) * Chunk::chunkHeight * Chunk::chunkSize, blocks)) {
                Chunk chunk(position, std::move(blocks));
                timer->add(nowSeconds() - start);
                return chunk;
            }
            std::cerr << "Stored chunk at " << position.x << ", " << position.z << " is corrupt, regenerating it" << std::endl;
        }
        Chunk chunk(position, *terrainPtr);
        timer->add(nowSeconds() - start);
        return chunk;
        });
}

void World::tryApplyChunkGeneration(bool waitForAll) {
    for (auto it = chunkGenerationFutures.begin(); it != chunkGenerationFutures.end();) {
        // waitForAll blocks on unfinished jobs (used before saving), otherwise only take the finished ones
        if (waitForAll || it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

            uint64_t key = it->first; // The key is the uint64_t chunk identifier
            auto inserted = chunks.emplace(key, it->second.get());
            Chunk& chunk = inserted.first->second;
            chunk.isActive = isInRange(key);//the player may have moved while it generated
            requestMesh(key);

            // Neighbours meshed without this chunk have faces and ambient occlusion on the shared border, rebuild them
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
    // All 8 surrounding chunks, diagonal ones use this chunk's corner column for ambient occlusion
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            if (findChunk(chunkX + dx, chunkZ + dz)) {
                requestMesh(getChunkKey(chunkX + dx, chunkZ + dz));
            }
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
            meshPool.release(it->second.mesh);
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
    const Chunk* neighbours[8] = {
        findChunk(chunkX + 1, chunkZ), // +X
        findChunk(chunkX - 1, chunkZ), // -X
        findChunk(chunkX, chunkZ + 1), // +Z
        findChunk(chunkX, chunkZ - 1), // -Z
        findChunk(chunkX + 1, chunkZ + 1), // +X+Z
        findChunk(chunkX + 1, chunkZ - 1), // +X-Z
        findChunk(chunkX - 1, chunkZ + 1), // -X+Z
        findChunk(chunkX - 1, chunkZ - 1)  // -X-Z
    };

    ChunkMeshInput input = chunk.makeMeshInput(neighbours);
    // Cleared before the job runs, so edits made while it runs queue another rebuild
    chunk.fullRebuildNeeded = false;
    meshQueue.erase(key);

    MeshJob& job = chunkMeshFutures[key];
    job.editTime = chunk.pendingEditTime;//this job's copy includes that edit
    chunk.pendingEditTime = -1.0;
    JobTimer* timer = &meshTimer;
    job.future = std::async(std::launch::async, [input = std::move(input), timer]() {
        double start = nowSeconds();
        MeshData mesh = Chunk::buildMesh(input);
        timer->add(nowSeconds() - start);
        return mesh;
        });
}

void World::processChunkMeshing(const glm::vec3& playerPosition) {
    // Upload finished meshes
    for (auto it = chunkMeshFutures.begin(); it != chunkMeshFutures.end();) {
        MeshJob& job = it->second;
        if (job.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        MeshData mesh = job.future.get();
        if (Chunk* chunk = findChunk(chunkKeyX(it->first), chunkKeyZ(it->first))) {
            meshPool.upload(chunk->mesh, mesh.vertices);
            chunk->currentTallestBlock = mesh.tallestBlock;
            // The edit is on screen from the next frame
            if (job.editTime >= 0.0) editLatencyTimer.add(nowSeconds() - job.editTime);
        }// else the chunk was unloaded while meshing
        it = chunkMeshFutures.erase(it);
    }

    if (chunkMeshFutures.size() >= maxAsyncTasks()) return;
    size_t freeSlots = maxAsyncTasks() - chunkMeshFutures.size();

    // Chunks waiting for a mesh, closest to the player first
    std::vector<std::pair<float, uint64_t>> waiting;
    for (auto it = meshQueue.begin(); it != meshQueue.end();) {
        const Chunk* chunk = findChunk(chunkKeyX(*it), chunkKeyZ(*it));
        if (!chunk || !chunk->fullRebuildNeeded) {
            it = meshQueue.erase(it);//unloaded since it was queued
            continue;
        }
        // Inactive chunks stay queued, they get meshed if the player comes back into range
        bool running = chunkMeshFutures.find(*it) != chunkMeshFutures.end();//one job per chunk at a time
        if (chunk->isActive && !running) {
            float dx = chunk->chunkPosition.x + Chunk::chunkSize * 0.5f - playerPosition.x;
            float dz = chunk->chunkPosition.z + Chunk::chunkSize * 0.5f - playerPosition.z;
            waiting.emplace_back(dx * dx + dz * dz, *it);
        }
        ++it;
    }

    size_t launchCount = std::min(freeSlots, waiting.size());
    std::partial_sort(waiting.begin(), waiting.begin() + launchCount, waiting.end());
    for (size_t i = 0; i < launchCount; i++) {
        uint64_t key = waiting[i].second;
        launchChunkMesh(key, chunks.at(key));
    }
}

void World::requestMesh(uint64_t key) {
    if (Chunk* chunk = findChunk(chunkKeyX(key), chunkKeyZ(key))) {
        chunk->fullRebuildNeeded = true;
        meshQueue.insert(key);
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
    requestMesh(getChunkKey(chunkX, chunkZ));
    if (chunk->pendingEditTime < 0.0) chunk->pendingEditTime = nowSeconds();

    // Blocks on the edge change the neighbour's visible faces and ambient occlusion too
    auto rebuild = [this](int x, int z) { requestMesh(getChunkKey(x, z)); };
    int edgeX = (localX == 0) ? -1 : (localX == Chunk::chunkSize - 1) ? 1 : 0;
    int edgeZ = (localZ == 0) ? -1 : (localZ == Chunk::chunkSize - 1) ? 1 : 0;
    if (edgeX != 0) rebuild(chunkX + edgeX, chunkZ);
    if (edgeZ != 0) rebuild(chunkX, chunkZ + edgeZ);
    if (edgeX != 0 && edgeZ != 0) rebuild(chunkX + edgeX, chunkZ + edgeZ);//corner block, diagonal chunk's AO
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
