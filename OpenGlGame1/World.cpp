#include "World.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <limits>
#include <thread>
#include <cmath>

// One worker per core, minus one for the main thread (it renders and uploads meshes)
static size_t workerCount() {
    return std::max(1u, std::thread::hardware_concurrency() - 1);
}

// Main thread time allowed per frame for uploading finished meshes, and for preparing new mesh jobs (each)
// Doing every available one in a single frame caused stutters when many finished together;
// leftovers just wait a frame (about 1 ms at these frame rates). At least one always runs so loading never stalls
// The first load gets a much bigger budget: nothing is happening yet, so finishing sooner matters more than smoothness
static const double meshingBudgetSeconds = 0.0005;
static const double initialMeshingBudgetSeconds = 0.004;

// Mesh jobs allowed to be waiting or running at once, per worker
// A mesh takes ~0.01 ms, so with only one each the workers finished in microseconds and sat idle until next frame:
// meshing was capped at (workers x frame rate) jobs a second, which flying at render distance 200 needs several times over
// (every chunk crossed loads a row of chunks and moves the LOD rings). The time budgets above still keep frames smooth
static const size_t meshJobsPerWorker = 8;

static int floorDiv(int a, int b) {
    return static_cast<int>(std::floor(static_cast<float>(a) / b));
}

World::World(int seed, int renderDistance) : terrain(seed), workers(workerCount()), renderDistance(renderDistance) {
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
    // Parked sections are outside render distance, so they don't count
    return meshHeap.empty();
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
        for (const ChunkSection& section : chunk.sections) stats.blockBytes += section.memoryBytes();
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
            snapshot.store(pair.first, encodeBlocks(pair.second.flatBlocks()));
        }
    }
    return snapshot.saveToFile(path);
}

bool World::loadChunks(const std::string& path) {
    return storage.loadFromFile(path);
}

World::~World() {
    // Jobs read terrain and add to the timers, so wait for every one before those are destroyed
    // (thread pool futures don't wait by themselves when destroyed)
    for (auto& pair : chunkGenerationFutures) pair.second.wait();
    for (auto& pair : chunkMeshFutures) pair.second.future.wait();
}

void World::update(const glm::vec3& playerPosition, const LodView& view) {
    // Calculate the player's chunk position in chunk coordinates
    glm::ivec3 playerChunkPos(
        static_cast<int>(floor(playerPosition.x / Chunk::chunkSize)),
        0, // Y is fixed at 0 for chunk coordinates
        static_cast<int>(floor(playerPosition.z / Chunk::chunkSize))
    );

    // Chunks only enter or leave render distance when the player crosses into another chunk,
    // so the work that looks at every chunk only runs then, not every frame
    auto msSince = [](double start) { return static_cast<float>((nowSeconds() - start) * 1000.0); };
    updateTimes = UpdateTimes();
    double start = nowSeconds();
    lastPlayerPosition = playerPosition;
    bool zoomChanged = view.zoom != lodView.zoom;
    lodView = view;
    float moved = 0.0f;//chunks the player moved since the last update, for the LOD rings
    if (areaChanged || playerChunkPos != lastPlayerChunk) {
        glm::vec2 move(playerChunkPos.x - lastPlayerChunk.x, playerChunkPos.z - lastPlayerChunk.z);
        moved = glm::length(move);
        loadCursor = areaChanged ? 0 : cursorAfterMove(playerChunkPos - lastPlayerChunk);
        lastPlayerChunk = playerChunkPos;
        areaChanged = false;
        updateActiveChunks();
        unloadFarChunks(playerChunkPos);
        refreshMeshHeap();
    }

    // Levels of detail, after the block above so distances are from the new player chunk
    // A big jump (teleport, render distance change) is cheaper to check all at once than as very wide rings
    bool zoomed = view.zoom > 1.0f;
    if (lodsChanged || moved > 8.0f) {
        updateAllLods();
        lodsChanged = false;
    }
    else if (moved > 0.0f) {
        updateLodRings(moved);
    }
    if (zoomChanged && !zoomed) endZoomLods();
    // Turning while zoomed only needs a recheck once the view has moved a little (1 degree), not every frame
    if (zoomed && (zoomChanged || moved > 0.0f || glm::dot(view.forward, lastLodForward) < 0.99985f)) {
        updateZoomedLods();
        lastLodForward = view.forward;
    }
    updateTimes.areaMs = msSince(start);

    start = nowSeconds();
    tryApplyChunkGeneration();//first, so its finished jobs free up workers for updateLoading
    updateTimes.applyMs = msSince(start);

    start = nowSeconds();
    updateLoading(playerChunkPos);
    updateTimes.loadMs = msSince(start);

    processChunkMeshing(playerPosition);//times its own two halves
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
    lodsChanged = true;//levels start at a percentage of render distance

    // Room for everything in range plus the unload margin, so inserting chunks never has to rehash the whole map
    size_t side = static_cast<size_t>(2 * (renderDistance + unloadMargin) + 1);
    chunks.reserve(side * side);
}

size_t World::cursorAfterMove(const glm::ivec3& move) const {
    // Starting over from 0 would re-check all 16k nearby chunks in one frame (a stutter every chunk border)
    // But chunks that were loaded before the move and are still close are still loaded,
    // so only the part of loadOrder further out than that needs checking again
    //
    // Before the move everything closer than `covered` was loaded or generating:
    // the whole render distance if loading had finished, otherwise everything before the cursor
    float covered = loadCursor >= loadOrder.size()
        ? static_cast<float>(renderDistance)
        : std::sqrt(static_cast<float>(loadOrder[loadCursor].x * loadOrder[loadCursor].x + loadOrder[loadCursor].y * loadOrder[loadCursor].y)) - 0.01f;
    // A chunk at offset o now was at o + move before, so it's still covered if |o| + |move| <= covered
    float moved = std::sqrt(static_cast<float>(move.x * move.x + move.z * move.z));
    float safe = covered - moved;
    if (safe <= 0.0f) return 0;

    // loadOrder is sorted by distance, so binary search for the first entry past the safe radius
    float safeSquared = safe * safe;
    auto first = std::partition_point(loadOrder.begin(), loadOrder.end(), [safeSquared](const glm::ivec2& o) {
        return static_cast<float>(o.x * o.x + o.y * o.y) <= safeSquared;
    });
    return static_cast<size_t>(first - loadOrder.begin());
}

void World::updateLoading(const glm::ivec3& playerChunkPos) {
    // Start as many generation jobs as there are free workers, nearest missing chunks first
    // (a fixed number per frame made loading speed depend on the frame rate instead of the CPU)
    size_t running = chunkGenerationFutures.size();
    while (loadCursor < loadOrder.size() && running < workers.size()) {
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

    chunkGenerationFutures[key] = workers.submit([terrainPtr, timer, position, savedData = std::move(savedData)]() {
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
            chunk.lod = static_cast<uint8_t>(targetLod(key, chunk));//meshed at the right detail straight away
            addToRegion(key, chunk);
            requestColumnMesh(key);

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
            requestColumnMesh(getChunkKey(chunkX + dx, chunkZ + dz));
        }
    }
}

void World::findNeighbours(uint64_t key, const Chunk* neighbours[8]) const {
    int chunkX = chunkKeyX(key);
    int chunkZ = chunkKeyZ(key);
    neighbours[0] = findChunk(chunkX + 1, chunkZ); // +X
    neighbours[1] = findChunk(chunkX - 1, chunkZ); // -X
    neighbours[2] = findChunk(chunkX, chunkZ + 1); // +Z
    neighbours[3] = findChunk(chunkX, chunkZ - 1); // -Z
    neighbours[4] = findChunk(chunkX + 1, chunkZ + 1); // +X+Z
    neighbours[5] = findChunk(chunkX + 1, chunkZ - 1); // +X-Z
    neighbours[6] = findChunk(chunkX - 1, chunkZ + 1); // -X+Z
    neighbours[7] = findChunk(chunkX - 1, chunkZ - 1); // -X-Z
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
                storage.store(it->first, encodeBlocks(it->second.flatBlocks()));
            }
            // Any mesh job still running for it only has a copy, its result is dropped when it finishes
            for (ChunkSection& section : it->second.sections) meshPool.release(section.mesh);
            removeFromRegion(it->first, it->second);
            it = chunks.erase(it);
        }
        else {
            ++it;
        }
    }
}

// ---------- meshing ----------

// Copy the chunk and its neighbour borders, then build the mesh on a worker thread
bool World::launchSectionMesh(const SectionKey& key, Chunk& chunk) {
    const Chunk* neighbours[8];
    findNeighbours(key.column, neighbours);
    ChunkSection& section = chunk.sections[key.section];

    // Cleared before the job runs, so edits made while it runs queue another rebuild
    section.rebuildNeeded = false;
    meshQueue.erase(key);

    // Open sky and solid rock: no faces, so no job, just make sure nothing old is still drawn
    if (chunk.sectionHasNoFaces(key.section, neighbours)) {
        meshPool.release(section.mesh);
        chunk.updateMeshedBit(key.section);
        section.pendingEditTime = -1.0;
        return false;
    }

    ChunkMeshInput input = chunk.makeMeshInput(key.section, neighbours);
    MeshJob& job = chunkMeshFutures[key];
    job.editTime = section.pendingEditTime;//this job's copy includes that edit
    job.lod = chunk.lod;
    section.pendingEditTime = -1.0;
    JobTimer* timer = &meshTimer;
    int lod = chunk.lod;
    job.future = workers.submit([input = std::move(input), timer, lod]() {
        double start = nowSeconds();
        MeshData mesh = Chunk::buildMesh(input, lod);
        timer->add(nowSeconds() - start);
        return mesh;
        });
    return true;
}

void World::processChunkMeshing(const glm::vec3& playerPosition) {
    if (!initialLoadComplete && isFullyLoaded()) initialLoadComplete = true;
    const double budget = initialLoadComplete ? meshingBudgetSeconds : initialMeshingBudgetSeconds;

    double start = nowSeconds();
    // Upload finished meshes
    for (auto it = chunkMeshFutures.begin(); it != chunkMeshFutures.end();) {
        if (nowSeconds() - start > budget) break;//rest next frame
        MeshJob& job = it->second;
        if (job.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        MeshData mesh = job.future.get();
        if (Chunk* chunk = findChunk(chunkKeyX(it->first.column), chunkKeyZ(it->first.column))) {
            meshPool.upload(chunk->sections[it->first.section].mesh, mesh.vertices);
            chunk->sections[it->first.section].meshLod = job.lod;
            chunk->updateMeshedBit(it->first.section);
            // The edit is on screen from the next frame
            if (job.editTime >= 0.0) editLatencyTimer.add(nowSeconds() - job.editTime);
        }// else the chunk was unloaded while meshing
        it = chunkMeshFutures.erase(it);
    }
    updateTimes.uploadMs = static_cast<float>((nowSeconds() - start) * 1000.0);
    start = nowSeconds();
    struct LaunchTimer {//records however this function returns
        UpdateTimes& times; double start;
        ~LaunchTimer() { times.launchMs = static_cast<float>((nowSeconds() - start) * 1000.0); }
    } launchTimer{ updateTimes, start };

    size_t maxJobs = workers.size() * meshJobsPerWorker;
    if (chunkMeshFutures.size() >= maxJobs) return;
    size_t freeSlots = maxJobs - chunkMeshFutures.size();

    // Nearest queued sections first, straight off the heap
    // Sections that turn out to have no faces finish here without a job, so they don't use up a worker
    size_t launched = 0;
    bool first = true;
    std::vector<QueuedMesh> stillRunning;//edited again while their last job runs, retried next frame
    while (!meshHeap.empty() && launched < freeSlots) {
        if (!first && nowSeconds() - start > budget) break;//rest next frame
        first = false;
        QueuedMesh next = meshHeap.top();
        meshHeap.pop();

        Chunk* chunk = findChunk(chunkKeyX(next.key.column), chunkKeyZ(next.key.column));
        if (!chunk || !chunk->sections[next.key.section].rebuildNeeded) {
            meshQueue.erase(next.key);//unloaded or already done since it was queued
            continue;
        }
        if (chunkMeshFutures.find(next.key) != chunkMeshFutures.end()) {//one job per section at a time
            stillRunning.push_back(next);
            continue;
        }
        if (!chunk->isActive) {
            parkedMeshes.push_back(next.key);//still in meshQueue, meshed if the player comes back into range
            continue;
        }
        if (launchSectionMesh(next.key, *chunk)) launched++;
    }
    for (const QueuedMesh& queued : stillRunning) meshHeap.push(queued);
}

void World::pushMesh(const SectionKey& key, const Chunk& chunk) {
    glm::vec3 offset = chunk.sectionOrigin(key.section) + glm::vec3(ChunkSection::size * 0.5f) - lastPlayerPosition;
    meshHeap.push({ glm::dot(offset, offset), key });
}

void World::refreshMeshHeap() {
    std::vector<SectionKey> keys = std::move(parkedMeshes);
    parkedMeshes.clear();
    while (!meshHeap.empty()) {
        keys.push_back(meshHeap.top().key);
        meshHeap.pop();
    }
    for (const SectionKey& key : keys) {
        const Chunk* chunk = findChunk(chunkKeyX(key.column), chunkKeyZ(key.column));
        if (chunk && chunk->sections[key.section].rebuildNeeded) pushMesh(key, *chunk);
        else meshQueue.erase(key);//unloaded since it was queued
    }
}

static uint64_t regionKeyFor(uint64_t chunkKey) {
    return getChunkKey(floorDiv(chunkKeyX(chunkKey), ChunkRegion::regionSize), floorDiv(chunkKeyZ(chunkKey), ChunkRegion::regionSize));
}

void World::addToRegion(uint64_t key, const Chunk& chunk) {
    uint64_t regionKey = regionKeyFor(key);
    ChunkRegion& region = regions[regionKey];
    region.coords = glm::ivec2(chunkKeyX(regionKey), chunkKeyZ(regionKey));
    region.chunks.push_back(&chunk);
}

void World::removeFromRegion(uint64_t key, const Chunk& chunk) {
    auto it = regions.find(regionKeyFor(key));
    if (it == regions.end()) return;
    std::vector<const Chunk*>& list = it->second.chunks;
    auto found = std::find(list.begin(), list.end(), &chunk);//at most 64, order doesn't matter
    if (found != list.end()) {
        *found = list.back();
        list.pop_back();
    }
    if (list.empty()) regions.erase(it);
}

void World::requestMesh(const SectionKey& key) {
    if (Chunk* chunk = findChunk(chunkKeyX(key.column), chunkKeyZ(key.column))) {
        chunk->sections[key.section].rebuildNeeded = true;
        if (meshQueue.insert(key).second) pushMesh(key, *chunk);//not already queued
    }
}

void World::requestColumnMesh(uint64_t key) {
    Chunk* chunk = findChunk(chunkKeyX(key), chunkKeyZ(key));
    if (!chunk) return;
    for (int s = 0; s < Chunk::sectionCount; s++) {
        const ChunkSection& section = chunk->sections[s];
        // All air with nothing drawn: neighbours changing can't give it faces, skip it (most of the sky)
        if (section.isEmpty() && section.mesh.capacity == 0) continue;
        requestMesh({ key, s });
    }
}

// ---------- level of detail ----------

void World::setLodDistances(int percent2x, int percent4x, int percent8x) {
    if (percent2x == lodPercent[0] && percent4x == lodPercent[1] && percent8x == lodPercent[2]) return;
    lodPercent[0] = percent2x;
    lodPercent[1] = percent4x;
    lodPercent[2] = percent8x;
    lodsChanged = true;
}

float World::lodStart(int level) const {
    // Each level starts at its percentage of render distance, and never closer than the level before it
    float start = minFullDetailChunks;
    for (int i = 0; i < level; i++) {
        if (lodPercent[i] >= 100) return std::numeric_limits<float>::infinity();//this level and the coarser ones are off
        start = std::max(start, lodPercent[i] * 0.01f * renderDistance);
    }
    return start;
}

int World::lodForDistance(float chunks) const {
    int lod = 0;
    while (lod + 1 < Chunk::lodLevels && chunks >= lodStart(lod + 1)) lod++;
    return lod;
}

// Distance in chunks from the player's chunk to a column, circular like the load order
static float chunkDistance(uint64_t key, const glm::ivec3& playerChunk) {
    float dx = static_cast<float>(chunkKeyX(key) - playerChunk.x);
    float dz = static_cast<float>(chunkKeyZ(key) - playerChunk.z);
    return std::sqrt(dx * dx + dz * dz);
}

int World::targetLod(uint64_t key, const Chunk& chunk) const {
    float distance = chunkDistance(key, lastPlayerChunk);
    int lod = lodForDistance(distance);
    // 4x zoom makes something 40 chunks away look like it's 10 away, so it gets the detail of 10 chunks away
    // Only inside the zoomed view though, the columns behind you don't look any closer
    if (lodView.zoom > 1.0f && lod > 0) {
        glm::vec3 min = chunk.chunkPosition;
        glm::vec3 max = min + glm::vec3(Chunk::chunkSize, Chunk::chunkHeight, Chunk::chunkSize);
        if (lodView.zoomedView.isBoxInFrustum(min, max)) lod = std::min(lod, lodForDistance(distance / lodView.zoom));
    }
    return lod;
}

// How far past a boundary a column has to get before it drops to less detail
// Without it a column right on the line would flip back and forth (and remesh) as the player walks along it
static const float lodHysteresisChunks = 1.0f;

void World::checkLod(uint64_t key, Chunk& chunk) {
    bool zoomed = lodView.zoom > 1.0f;
    int current = chunk.lod;
    int want = targetLod(key, chunk);
    if (want > current) {
        // While zoomed, detail is only ever added: looking around would otherwise remesh everything
        // the view sweeps past twice (up on the way in, down on the way out). It all drops back when zoom ends
        if (zoomed) return;
        want = std::max(current, lodForDistance(std::max(0.0f, chunkDistance(key, lastPlayerChunk) - lodHysteresisChunks)));
    }
    if (want == current) return;
    if (zoomed) zoomUpgraded.push_back(key);
    chunk.lod = static_cast<uint8_t>(want);
    requestColumnMesh(key);//the old mesh stays on screen until the new one is uploaded, so nothing flickers
}

void World::updateAllLods() {
    for (auto& pair : chunks) checkLod(pair.first, pair.second);
}

void World::updateLodRings(float moved) {
    // Moving `moved` chunks changes every distance by at most that much, so a column can only change level
    // if it was or now is within that of a boundary (plus the hysteresis on the way out)
    // loadOrder is every offset sorted by distance, so each ring is one slice of it found by binary search
    auto firstAtLeast = [&](float distance) {
        float squared = std::max(0.0f, distance) * std::max(0.0f, distance);
        return std::partition_point(loadOrder.begin(), loadOrder.end(), [squared](const glm::ivec2& o) {
            return static_cast<float>(o.x * o.x + o.y * o.y) < squared;
        });
    };
    for (int level = 1; level < Chunk::lodLevels; level++) {
        float start = lodStart(level);
        if (start > renderDistance * 1.5f) break;//off, or past the corners of the loaded square
        auto first = firstAtLeast(start - moved - 1.0f);
        auto last = firstAtLeast(start + lodHysteresisChunks + moved + 1.0f);
        for (auto it = first; it != last; ++it) {
            int chunkX = lastPlayerChunk.x + it->x, chunkZ = lastPlayerChunk.z + it->y;
            if (Chunk* chunk = findChunk(chunkX, chunkZ)) checkLod(getChunkKey(chunkX, chunkZ), *chunk);
        }
    }
}

void World::endZoomLods() {
    std::vector<uint64_t> keys = std::move(zoomUpgraded);
    zoomUpgraded.clear();
    for (uint64_t key : keys) {
        if (Chunk* chunk = findChunk(chunkKeyX(key), chunkKeyZ(key))) checkLod(key, *chunk);//unloaded ones are gone anyway
    }
}

void World::updateZoomedLods() {
    // Just the regions the zoomed view can see, the same test the renderer uses to skip them
    const float regionWidth = static_cast<float>(ChunkRegion::regionSize * Chunk::chunkSize);
    const float bottom = static_cast<float>(-Chunk::baseTerrainHeight);
    for (const auto& pair : regions) {
        glm::vec3 min(pair.second.coords.x * regionWidth, bottom, pair.second.coords.y * regionWidth);
        glm::vec3 max = min + glm::vec3(regionWidth, static_cast<float>(Chunk::chunkHeight), regionWidth);
        if (!lodView.zoomedView.isBoxInFrustum(min, max)) continue;
        for (const Chunk* regionChunk : pair.second.chunks) {
            // Regions only hold const pointers, so find the chunk again by its key (positions are whole chunks)
            int chunkX = static_cast<int>(regionChunk->chunkPosition.x) / Chunk::chunkSize;
            int chunkZ = static_cast<int>(regionChunk->chunkPosition.z) / Chunk::chunkSize;
            if (Chunk* chunk = findChunk(chunkX, chunkZ)) checkLod(getChunkKey(chunkX, chunkZ), *chunk);
        }
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
    if (!chunk->setBlock(localX, localY, localZ, type)) return;//already that block

    int sectionIndex = localY / ChunkSection::size;
    int sectionY = localY % ChunkSection::size;
    ChunkSection& section = chunk->sections[sectionIndex];
    if (section.pendingEditTime < 0.0) section.pendingEditTime = nowSeconds();

    // The edited section, plus every neighbouring section the block is touching:
    // faces and ambient occlusion reach one block across section borders, sideways, up/down and diagonally
    const int last = ChunkSection::size - 1;
    int edgeX = (localX == 0) ? -1 : (localX == last) ? 1 : 0;
    int edgeY = (sectionY == 0) ? -1 : (sectionY == last) ? 1 : 0;
    int edgeZ = (localZ == 0) ? -1 : (localZ == last) ? 1 : 0;
    for (int dx : { 0, edgeX }) {//when an edge is 0 both entries are the same, requestMesh doesn't mind repeats
        for (int dy : { 0, edgeY }) {
            for (int dz : { 0, edgeZ }) {
                int s = sectionIndex + dy;
                if (s < 0 || s >= Chunk::sectionCount) continue;
                requestMesh({ getChunkKey(chunkX + dx, chunkZ + dz), s });
            }
        }
    }
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
