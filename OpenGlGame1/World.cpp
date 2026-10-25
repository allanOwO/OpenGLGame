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

// A region is merged again once nothing in it has changed for this long, so a region that's streaming in
// (64 columns, each meshed and then remeshed as neighbours arrive) is copied once instead of dozens of times
// Until then the new sections are drawn on their own, so nothing is missing in the meantime
static const double regionSettleSeconds = 0.25;
static const double mergeBudgetSeconds = 0.001;
static const double lodBudgetSeconds = 0.001;//main thread time per frame for re-picking levels of detail

static float chunkDistance(uint64_t key, const glm::ivec3& playerChunk);//defined with the LOD code

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
    if (!chunkGenerationFutures.empty() || !chunkMeshFutures.empty() || !dirtyRegions.empty() || !lodQueue.empty()) return false;
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
    stats.dirtyRegions = static_cast<int>(dirtyRegions.size());
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
        checkSimulationBoundary();
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
    // Turning while zoomed only needs a recheck once the view has turned an eighth of its width
    // (16x zoom is only ~7 degrees wide, so a fixed angle would be either too often at 4x or too rarely at 16x)
    float viewWidth = 2.0f * std::atan(1.0f / std::max(view.zoom, 1.0f));//vertical field of view in radians
    bool turned = glm::dot(view.forward, lastLodForward) < std::cos(std::max(viewWidth / 8.0f, glm::radians(0.5f)));
    if (zoomChanged || (zoomed && (moved > 0.0f || turned))) {
        updateZoomedLods();
        lastLodForward = view.forward;
    }
    updateTimes.areaMs = msSince(start);

    start = nowSeconds();
    processLodQueue();
    updateTimes.lodMs = msSince(start);

    start = nowSeconds();
    tryApplyChunkGeneration();//first, so its finished jobs free up workers for updateLoading
    updateTimes.applyMs = msSince(start);

    start = nowSeconds();
    updateLoading(playerChunkPos);
    updateTimes.loadMs = msSince(start);

    processChunkMeshing(playerPosition);//times its own two halves

    start = nowSeconds();
    rebuildDirtyRegions();//after meshing, so this frame's uploads are included
    updateTimes.mergeMs = msSince(start);
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
    // Same for the mesh queue: zooming can queue a few sections for every column at once,
    // and rehashing 130k entries in the middle of a frame took 10+ ms
    meshQueue.reserve(side * side * 4);
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
        sectionMeshChanged(key.column, chunk, key.section);
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
    int chunkX = chunkKeyX(key.column), chunkZ = chunkKeyZ(key.column);
    uint16_t place = packPlace(chunkX - floorDiv(chunkX, ChunkRegion::regionSize) * ChunkRegion::regionSize,
        chunkZ - floorDiv(chunkZ, ChunkRegion::regionSize) * ChunkRegion::regionSize, key.section);
    job.future = workers.submit([input = std::move(input), timer, lod, place]() {
        double start = nowSeconds();
        MeshData mesh = Chunk::buildMesh(input, lod);
        for (PackedVertex& vertex : mesh.vertices) vertex.place = place;
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
            sectionMeshChanged(it->first.column, *chunk, it->first.section);
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

void World::addToRegion(uint64_t key, Chunk& chunk) {
    uint64_t regionKey = regionKeyFor(key);
    ChunkRegion& region = regions[regionKey];
    region.coords = glm::ivec2(chunkKeyX(regionKey), chunkKeyZ(regionKey));
    region.chunks.push_back(&chunk);
}

void World::removeFromRegion(uint64_t key, Chunk& chunk) {
    auto it = regions.find(regionKeyFor(key));
    if (it == regions.end()) return;
    std::vector<Chunk*>& list = it->second.chunks;
    auto found = std::find(list.begin(), list.end(), &chunk);//at most 64, order doesn't matter
    if (found != list.end()) {
        *found = list.back();
        list.pop_back();
    }
    if (list.empty()) {
        meshPool.release(it->second.merged);
        dirtyRegions.erase(it->first);
        regions.erase(it);
    }
    else if (chunk.mergedSections != 0) {
        markRegionDirty(key);//its merged mesh still has this chunk in it
    }
}

// ---------- merged regions ----------

void World::setSimulationDistance(int chunks) {
    if (chunks == simulationDistance) return;
    simulationDistance = chunks;
    for (const auto& pair : regions) dirtyRegions.insert(pair.first);//every column might be on the other side now
}

bool World::mergesColumn(uint64_t key) const {
    return simulationDistance < renderDistance && chunkDistance(key, lastPlayerChunk) > simulationDistance;
}

void World::markRegionDirty(uint64_t chunkKey) {
    uint64_t regionKey = regionKeyFor(chunkKey);
    auto it = regions.find(regionKey);
    if (it == regions.end()) return;
    it->second.dirtyTime = nowSeconds();
    dirtyRegions.insert(regionKey);
}

void World::sectionMeshChanged(uint64_t key, Chunk& chunk, int section) {
    // In the merged mesh: that copy is now old. Outside simulation distance: it should join the merged mesh
    // Neither (close to the player, the usual case for edits): it's drawn on its own, nothing to redo
    bool merged = chunk.mergedSections & (1u << section);
    if (merged) chunk.sections[section].mergedCount = 0;//the merged mesh still draws the old one until it's rebuilt
    if (merged || mergesColumn(key)) markRegionDirty(key);
}

void World::checkSimulationBoundary() {
    // Only regions the simulation distance circle passes through can have a column that crossed it
    for (auto& pair : regions) {
        ChunkRegion& region = pair.second;
        // Nearest and furthest column of the region from the player, per axis then combined
        int minX = region.coords.x * ChunkRegion::regionSize - lastPlayerChunk.x;
        int minZ = region.coords.y * ChunkRegion::regionSize - lastPlayerChunk.z;
        int maxX = minX + ChunkRegion::regionSize - 1, maxZ = minZ + ChunkRegion::regionSize - 1;
        auto nearest = [](int low, int high) { return low > 0 ? low : (high < 0 ? -high : 0); };
        float nearX = static_cast<float>(nearest(minX, maxX)), nearZ = static_cast<float>(nearest(minZ, maxZ));
        float farX = static_cast<float>(std::max(std::abs(minX), std::abs(maxX)));
        float farZ = static_cast<float>(std::max(std::abs(minZ), std::abs(maxZ)));
        float sim = static_cast<float>(simulationDistance);
        if (std::sqrt(nearX * nearX + nearZ * nearZ) > sim + 1.0f) continue;//entirely outside
        if (std::sqrt(farX * farX + farZ * farZ) < sim - 1.0f) continue;//entirely inside

        for (Chunk* chunk : region.chunks) {
            int chunkX = static_cast<int>(chunk->chunkPosition.x) / Chunk::chunkSize;
            int chunkZ = static_cast<int>(chunk->chunkPosition.z) / Chunk::chunkSize;
            uint64_t key = getChunkKey(chunkX, chunkZ);
            if (chunk->mergeWanted != mergesColumn(key)) {
                markRegionDirty(key);
                break;
            }
        }
    }
}

void World::rebuildRegion(ChunkRegion& region) {
    // Each section's newest vertices are either its own mesh (meshed since the last merge, or never merged)
    // or its slice of the current merged mesh. The new merged mesh is copied together from those
    struct Member { ChunkSection* section; GLint offset; };
    std::vector<MeshRange> parts;
    std::vector<Member> members;
    GLint offset = 0;
    for (Chunk* chunk : region.chunks) {
        int chunkX = static_cast<int>(chunk->chunkPosition.x) / Chunk::chunkSize;
        int chunkZ = static_cast<int>(chunk->chunkPosition.z) / Chunk::chunkSize;
        chunk->mergeWanted = mergesColumn(getChunkKey(chunkX, chunkZ));
        for (int s = 0; s < Chunk::sectionCount; s++) {
            ChunkSection& section = chunk->sections[s];
            bool hasSlice = (chunk->mergedSections & (1u << s)) && section.mergedCount > 0;
            if (!chunk->mergeWanted) {
                // Back inside simulation distance: it'll be drawn on its own again, so it needs its own copy back
                // (done now, the old merged mesh is released at the end of this)
                if (hasSlice && section.mesh.vertexCount == 0) {
                    meshPool.combine(section.mesh, { { region.merged.page, region.merged.firstVertex + section.mergedFirst, section.mergedCount } });
                }
                section.mergedCount = 0;
                continue;
            }
            MeshRange source{ 0, 0, 0 };
            if (section.mesh.vertexCount > 0) source = { section.mesh.page, section.mesh.firstVertex, section.mesh.vertexCount };
            else if (hasSlice) source = { region.merged.page, region.merged.firstVertex + section.mergedFirst, section.mergedCount };
            if (source.count == 0) continue;//nothing to draw
            parts.push_back(source);
            members.push_back({ &section, offset });
            offset += source.count;
        }
    }
    meshPool.combine(region.merged, parts);

    // Every member now lives in the merged mesh only, and is drawn as part of it instead of on its own
    for (Chunk* chunk : region.chunks) chunk->mergedSections = 0;
    for (const Member& member : members) {
        member.section->mergedFirst = member.offset;
        member.section->mergedCount = static_cast<GLsizei>(parts[&member - members.data()].count);
        meshPool.release(member.section->mesh);
    }
    for (Chunk* chunk : region.chunks) {
        for (int s = 0; s < Chunk::sectionCount; s++) {
            if (chunk->sections[s].mergedCount > 0) chunk->mergedSections |= static_cast<uint16_t>(1u << s);
            chunk->updateMeshedBit(s);
        }
    }
}

void World::rebuildDirtyRegions() {
    double start = nowSeconds();
    for (auto it = dirtyRegions.begin(); it != dirtyRegions.end();) {
        if (nowSeconds() - start > mergeBudgetSeconds) break;//rest next frame
        auto region = regions.find(*it);
        if (region == regions.end()) {
            it = dirtyRegions.erase(it);
            continue;
        }
        if (start - region->second.dirtyTime < regionSettleSeconds) {
            ++it;//still changing
            continue;
        }
        rebuildRegion(region->second);
        it = dirtyRegions.erase(it);
    }
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
    float distance = chunkDistance(key, lastPlayerChunk);
    int current = chunk.lod;
    int want = targetLod(key, chunk);
    if (want > current) {
        // Still near the zoomed view: keep the detail, so panning back and forth doesn't remesh it every time
        glm::vec3 min = chunk.chunkPosition;
        glm::vec3 max = min + glm::vec3(Chunk::chunkSize, Chunk::chunkHeight, Chunk::chunkSize);
        bool nearView = lodView.zoom > 1.0f && lodView.keepView.isBoxInFrustum(min, max);
        if (!nearView) want = std::max(current, lodForDistance(std::max(0.0f, distance - lodHysteresisChunks)));
        else want = current;
    }
    if (want != current) {
        chunk.lod = static_cast<uint8_t>(want);
        requestColumnMesh(key);//the old mesh stays on screen until the new one is uploaded, so nothing flickers
    }
    chunk.zoomDetail = chunk.lod < lodForDistance(distance);
    if (chunk.zoomDetail) zoomRegions.insert(regionKeyFor(key));//so it's found again when zoom ends
}

void World::queueRegionLods(uint64_t regionKey) {
    if (lodQueued.insert(regionKey).second) lodQueue.push_back(regionKey);
}

void World::processLodQueue() {
    // A region (up to 64 columns) at a time, so the budget is checked often enough without timing every column
    double start = nowSeconds();
    while (!lodQueue.empty() && nowSeconds() - start < lodBudgetSeconds) {
        uint64_t regionKey = lodQueue.front();
        lodQueue.pop_front();
        lodQueued.erase(regionKey);
        auto it = regions.find(regionKey);
        if (it == regions.end()) {
            zoomRegions.erase(regionKey);//unloaded
            continue;
        }
        bool anyZoomDetail = false;
        for (Chunk* chunk : it->second.chunks) {
            int chunkX = static_cast<int>(chunk->chunkPosition.x) / Chunk::chunkSize;//positions are whole chunks
            int chunkZ = static_cast<int>(chunk->chunkPosition.z) / Chunk::chunkSize;
            checkLod(getChunkKey(chunkX, chunkZ), *chunk);
            anyZoomDetail |= chunk->zoomDetail;
        }
        if (anyZoomDetail) zoomRegions.insert(regionKey);
        else zoomRegions.erase(regionKey);
    }
}

void World::updateAllLods() {
    for (const auto& pair : regions) queueRegionLods(pair.first);
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

void World::updateZoomedLods() {
    // Regions with zoom detail first: they may have left the view (or zoom ended) and can drop back
    // Only regions are queued here, the columns are checked over the next frames by processLodQueue
    for (uint64_t regionKey : zoomRegions) queueRegionLods(regionKey);
    if (lodView.zoom <= 1.0f) return;

    // Then the regions the zoomed view can see, the same test the renderer uses to skip them
    // Nearest first, so the detail fills in from the middle of the view outwards
    const float regionWidth = static_cast<float>(ChunkRegion::regionSize * Chunk::chunkSize);
    const float bottom = static_cast<float>(-Chunk::baseTerrainHeight);
    std::vector<std::pair<int, uint64_t>> inView;//distance squared in regions, key
    glm::ivec2 playerRegion(floorDiv(lastPlayerChunk.x, ChunkRegion::regionSize), floorDiv(lastPlayerChunk.z, ChunkRegion::regionSize));
    for (const auto& pair : regions) {
        glm::vec3 min(pair.second.coords.x * regionWidth, bottom, pair.second.coords.y * regionWidth);
        glm::vec3 max = min + glm::vec3(regionWidth, static_cast<float>(Chunk::chunkHeight), regionWidth);
        if (!lodView.zoomedView.isBoxInFrustum(min, max)) continue;
        glm::ivec2 offset = pair.second.coords - playerRegion;
        inView.push_back({ offset.x * offset.x + offset.y * offset.y, pair.first });
    }
    std::sort(inView.begin(), inView.end());
    for (const auto& entry : inView) queueRegionLods(entry.second);
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
