#include "FarTerrain.h"
#include <algorithm>
#include <cmath>

#include "Chunk.h"
#include "ChunkKey.h"
#include "DebugStats.h"
#include "TerrainGenerator.h"

namespace {
	// A unit is built once nothing in it has changed for this long, same idea as World's merged regions:
	// near chunks streaming in at its edge would otherwise rebuild it every few frames
	const double settleSeconds = 0.25;
	const size_t jobsPerWorker = 2;

	int floorDiv(int a, int b) {
		return static_cast<int>(std::floor(static_cast<float>(a) / b));
	}

	// Closest and furthest a unit's chunks are from the player's chunk, square distance like the near tier's loading
	void unitDistances(const glm::ivec2& coords, const glm::ivec3& player, int& nearest, int& furthest) {
		int lowX = coords.x * FarUnit::unitChunks, highX = lowX + FarUnit::unitChunks - 1;
		int lowZ = coords.y * FarUnit::unitChunks, highZ = lowZ + FarUnit::unitChunks - 1;
		int nearX = std::max(0, std::max(lowX - player.x, player.x - highX));
		int nearZ = std::max(0, std::max(lowZ - player.z, player.z - highZ));
		nearest = std::max(nearX, nearZ);
		furthest = std::max(std::max(std::abs(lowX - player.x), std::abs(highX - player.x)),
			std::max(std::abs(lowZ - player.z), std::abs(highZ - player.z)));
	}

	// The heightfield mesh for one unit, on a worker
	// cell: blocks per heightfield cell. excluded: 32x32 chunks drawn by the near tier (empty = none)
	// edited: (chunk index z * 32 + x, highest block) for edited chunks that aren't loaded
	MeshData buildUnitMesh(const TerrainGenerator& terrain, glm::ivec2 coords, int cell,
		const std::vector<uint8_t>& excluded, const std::vector<std::pair<int, int>>& edited) {
		const int B = FarUnit::unitBlocks;
		const int n = B / cell;
		// Sample spacing, a cell's highest sample is its height. Zoomed cells (4, 8) sample every 2 blocks:
		// every block would be 4x the noise for detail too small to see even zoomed
		const int step = cell <= 8 ? 2 : cell / 4;
		const int side = B / step + 1;//the far edge too: it's the next unit's first column, so neighbours agree on it

		// Heights at every sample point, chunk local y like Chunk::heights
		float baseX = static_cast<float>(coords.x * B), baseZ = static_cast<float>(coords.y * B);
		std::vector<int> samples(static_cast<size_t>(side) * side);
		for (int sz = 0; sz < side; sz++)
			for (int sx = 0; sx < side; sx++)
				samples[sz * side + sx] = static_cast<int>(terrain.getHeight(baseX + sx * step, baseZ + sz * step));

		// Each cell is as high as the highest sample on or inside it, like LOD's "any block makes it solid"
		const int perCell = cell / step;
		std::vector<int> height(static_cast<size_t>(n) * n);
		for (int j = 0; j < n; j++) {
			for (int i = 0; i < n; i++) {
				int highest = 0;
				for (int sz = j * perCell; sz <= (j + 1) * perCell; sz++)
					for (int sx = i * perCell; sx <= (i + 1) * perCell; sx++)
						highest = std::max(highest, samples[sz * side + sx]);
				height[j * n + i] = highest;
			}
		}
		// An edited chunk raises every cell it covers (several when zoomed cells are smaller than a chunk)
		const int cellsPerChunk = std::max(1, Chunk::chunkSize / cell);
		for (const auto& entry : edited) {
			int chunkX = entry.first % FarUnit::unitChunks, chunkZ = entry.first / FarUnit::unitChunks;
			int firstI = chunkX * Chunk::chunkSize / cell, firstJ = chunkZ * Chunk::chunkSize / cell;
			for (int j = firstJ; j < firstJ + cellsPerChunk; j++)
				for (int i = firstI; i < firstI + cellsPerChunk; i++)
					height[j * n + i] = std::max(height[j * n + i], entry.second);
		}

		// Cells only line up with chunks at 16 blocks or less, which units next to the near tier always are
		auto shown = [&](int i, int j) {
			if (i < 0 || j < 0 || i >= n || j >= n) return false;
			if (excluded.empty()) return true;
			int chunkX = i * cell / Chunk::chunkSize, chunkZ = j * cell / Chunk::chunkSize;
			return !excluded[chunkZ * FarUnit::unitChunks + chunkX];
		};
		// A wall from floor up to top (exclusive), banded like generated chunks: grass on the top block,
		// dirt for the 3 under it, stone below. addBand(bottom, height, type) appends one quad
		auto wallBands = [](int floor, int top, auto addBand) {
			int grassFloor = std::max(floor, top - 1), dirtFloor = std::max(floor, top - 4);
			if (dirtFloor > floor) addBand(floor, dirtFloor - floor, BlockType::STONE);
			if (grassFloor > dirtFloor) addBand(dirtFloor, grassFloor - dirtFloor, BlockType::DIRT);
			addBand(grassFloor, top - grassFloor, BlockType::GRASS);
		};
		// Open edges (the unit's border, and around near chunks) have walls down past the lowest ground
		int lowest = Chunk::chunkHeight;
		for (int h : height) lowest = std::min(lowest, h);
		lowest = std::max(0, lowest - 8);

		MeshData groups[faceGroupCount];
		// Tops, cells next to each other at the same height joined into one quad
		for (int j = 0; j < n; j++) {
			for (int i = 0; i < n;) {
				if (!shown(i, j)) { i++; continue; }
				int h = height[j * n + i];
				int width = 1;
				while (shown(i + width, j) && height[j * n + i + width] == h) width++;
				// Facing +y the quad's sides run along z then x (see Chunk::appendQuad)
				Chunk::appendQuad(groups, glm::ivec3(i * cell, h + 1, j * cell), glm::ivec3(0, 0, cell), glm::ivec3(width * cell, 0, 0),
					1, 1, BlockType::GRASS);
				i += width;
			}
		}
		// Walls between cells, facing x then z. Each cell covers its own drop, down to its neighbour or to `lowest`
		for (int j = 0; j < n; j++) {
			for (int edge = 0; edge <= n; edge++) {
				bool left = shown(edge - 1, j), right = shown(edge, j);
				int leftTop = left ? height[j * n + edge - 1] + 1 : 0, rightTop = right ? height[j * n + edge] + 1 : 0;
				// Facing x the quad's sides run along y then z
				auto addX = [&](int direction) {
					return [&, direction](int bottom, int tall, BlockType type) {
						Chunk::appendQuad(groups, glm::ivec3(edge * cell, bottom, j * cell),
							glm::ivec3(0, tall, 0), glm::ivec3(0, 0, cell), 0, direction, type);
					};
				};
				if (left) {
					int floor = right ? rightTop : lowest;
					if (leftTop > floor) wallBands(floor, leftTop, addX(1));
				}
				if (right) {
					int floor = left ? leftTop : lowest;
					if (rightTop > floor) wallBands(floor, rightTop, addX(-1));
				}
			}
		}
		for (int i = 0; i < n; i++) {
			for (int edge = 0; edge <= n; edge++) {
				bool back = shown(i, edge - 1), front = shown(i, edge);
				int backTop = back ? height[(edge - 1) * n + i] + 1 : 0, frontTop = front ? height[edge * n + i] + 1 : 0;
				// Facing z the quad's sides run along x then y
				auto addZ = [&](int direction) {
					return [&, direction](int bottom, int tall, BlockType type) {
						Chunk::appendQuad(groups, glm::ivec3(i * cell, bottom, edge * cell),
							glm::ivec3(cell, 0, 0), glm::ivec3(0, tall, 0), 2, direction, type);
					};
				};
				if (back) {
					int floor = front ? frontTop : lowest;
					if (backTop > floor) wallBands(floor, backTop, addZ(1));
				}
				if (front) {
					int floor = back ? backTop : lowest;
					if (frontTop > floor) wallBands(floor, frontTop, addZ(-1));
				}
			}
		}

		MeshData mesh;
		for (int g = 0; g < faceGroupCount; g++) {
			mesh.groupCounts[g] = static_cast<int>(groups[g].vertices.size());
			mesh.vertices.insert(mesh.vertices.end(), groups[g].vertices.begin(), groups[g].vertices.end());
		}
		return mesh;
	}
}

glm::vec3 FarUnit::origin() const {
	float width = static_cast<float>(unitBlocks);
	return glm::vec3(coords.x * width, static_cast<float>(-Chunk::baseTerrainHeight), coords.y * width);
}

FarTerrain::FarTerrain(const TerrainGenerator& terrain, ThreadPool& workers, ChunkMeshPool& pool,
	std::function<bool(int, int)> nearShown, std::function<int(int, int)> editedTop)
	: terrain(terrain), workers(workers), pool(pool), nearShown(std::move(nearShown)), editedTop(std::move(editedTop)) {
}

FarTerrain::~FarTerrain() {
	for (auto& pair : jobs) pair.second.future.wait();
}

void FarTerrain::chunkChanged(int chunkX, int chunkZ) {
	auto it = units.find(getChunkKey(floorDiv(chunkX, FarUnit::unitChunks), floorDiv(chunkZ, FarUnit::unitChunks)));
	if (it == units.end()) return;
	it->second.dirty = true;
	it->second.dirtyTime = nowSeconds();
}

bool FarTerrain::isSettled() const {
	if (!jobs.empty()) return false;
	for (const auto& pair : units) {
		if (pair.second.dirty && !pair.second.leaving) return false;
	}
	return true;
}

void FarTerrain::setView(const LodView& view) {
	lodView = view;
	needsRefresh = true;
}

int FarTerrain::cellForDistance(float chunks) const {
	// Only zoom brings a unit this close: under half detail distance looks close enough for 4 block cells
	if (chunks <= detailDistance * 0.5f) return 4;
	if (chunks <= detailDistance) return 8;
	if (chunks <= detailDistance + FarUnit::unitChunks) return 16;
	return chunks <= detailDistance * 2 + 64 ? 32 : 64;
}

void FarTerrain::refreshUnits() {
	for (auto& pair : units) pair.second.leaving = true;//until shown to be needed below
	if (renderDistance <= detailDistance) return;//no far tier, everything is near chunks

	int lowX = floorDiv(playerChunk.x - renderDistance, FarUnit::unitChunks);
	int highX = floorDiv(playerChunk.x + renderDistance, FarUnit::unitChunks);
	int lowZ = floorDiv(playerChunk.z - renderDistance, FarUnit::unitChunks);
	int highZ = floorDiv(playerChunk.z + renderDistance, FarUnit::unitChunks);
	for (int uz = lowZ; uz <= highZ; uz++) {
		for (int ux = lowX; ux <= highX; ux++) {
			glm::ivec2 coords(ux, uz);
			int nearest, furthest;
			unitDistances(coords, playerChunk, nearest, furthest);
			// Needed if some of it is past detail distance and some of it is within render distance
			if (nearest > renderDistance || furthest <= detailDistance) continue;

			uint64_t key = getChunkKey(ux, uz);
			auto inserted = units.try_emplace(key);
			FarUnit& unit = inserted.first->second;
			if (inserted.second) unit.coords = coords;
			unit.leaving = false;

			// Coarser cells further out. Units that reach into the near tier have to use 16 or less
			// (cells inside one chunk) so they can leave out exactly the chunks the near tier draws
			int cell = std::max(16, cellForDistance(static_cast<float>(nearest)));//4 and 8 are only for zoom
			if (lodView.zoom > 1.0f) {
				// Inside the zoomed view it's the detail of something zoom times nearer, only ever finer,
				// the same as World::targetLod. Still near the view: keep it, so panning doesn't rebuild it
				glm::vec3 min = unit.origin();
				glm::vec3 max = min + glm::vec3(FarUnit::unitBlocks, Chunk::chunkHeight, FarUnit::unitBlocks);
				if (lodView.zoomedView.isBoxInFrustum(min, max))
					cell = std::min(cell, cellForDistance(nearest / lodView.zoom));
				if (unit.wantedCellSize != 0 && unit.wantedCellSize < cell && lodView.keepView.isBoxInFrustum(min, max))
					cell = unit.wantedCellSize;
			}

			if (unit.wantedCellSize != cell) {
				unit.wantedCellSize = cell;
				unit.dirty = true;
				unit.dirtyTime = 0.0;//straight away, there's nothing to wait for
			}
		}
	}
}

bool FarTerrain::coveredByNear(const FarUnit& unit) const {
	for (int z = 0; z < FarUnit::unitChunks; z++)
		for (int x = 0; x < FarUnit::unitChunks; x++)
			if (!nearShown(unit.coords.x * FarUnit::unitChunks + x, unit.coords.y * FarUnit::unitChunks + z)) return false;
	return true;
}

void FarTerrain::startBuild(uint64_t key, FarUnit& unit) {
	int cell = unit.wantedCellSize;
	int nearest, furthest;
	unitDistances(unit.coords, playerChunk, nearest, furthest);

	// Which chunks the near tier draws, only for units it reaches into (the rest have none)
	std::vector<uint8_t> excluded;
	std::vector<std::pair<int, int>> edited;
	int firstX = unit.coords.x * FarUnit::unitChunks, firstZ = unit.coords.y * FarUnit::unitChunks;
	bool touchesNear = nearest <= detailDistance + 1;
	if (touchesNear) excluded.assign(FarUnit::unitChunks * FarUnit::unitChunks, 0);
	for (int z = 0; z < FarUnit::unitChunks; z++) {
		for (int x = 0; x < FarUnit::unitChunks; x++) {
			int index = z * FarUnit::unitChunks + x;
			if (touchesNear) excluded[index] = nearShown(firstX + x, firstZ + z) ? 1 : 0;
			int top = editedTop(firstX + x, firstZ + z);
			if (top >= 0) edited.push_back({ index, top });
		}
	}

	unit.dirty = false;//anything that changes while it builds marks it again
	const TerrainGenerator* terrainPtr = &terrain;
	glm::ivec2 coords = unit.coords;
	Job& job = jobs[key];
	job.cellSize = cell;
	job.future = workers.submit([terrainPtr, coords, cell, excluded = std::move(excluded), edited = std::move(edited)]() {
		return buildUnitMesh(*terrainPtr, coords, cell, excluded, edited);
	});
}

void FarTerrain::update(const glm::ivec3& player, int detail, int render, double budgetSeconds) {
	if (player != playerChunk || detail != detailDistance || render != renderDistance || needsRefresh) {
		playerChunk = player;
		detailDistance = detail;
		renderDistance = render;
		needsRefresh = false;
		refreshUnits();
	}
	double start = nowSeconds();

	// Finished builds
	for (auto it = jobs.begin(); it != jobs.end();) {
		if (it->second.future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) { ++it; continue; }
		MeshData mesh = it->second.future.get();
		auto unit = units.find(it->first);
		if (unit != units.end()) {
			pool.upload(unit->second.mesh, mesh.vertices, mesh.groupCounts);
			unit->second.cellSize = it->second.cellSize;
		}
		it = jobs.erase(it);
	}

	// Units no longer needed: past render distance they just go, inside detail distance they stay
	// until the near chunks under them are all drawn, so nothing goes missing while they load
	bool checkedOne = false;
	for (auto it = units.begin(); it != units.end();) {
		FarUnit& unit = it->second;
		if (!unit.leaving || jobs.count(it->first)) { ++it; continue; }
		int nearest, furthest;
		unitDistances(unit.coords, playerChunk, nearest, furthest);
		bool remove = nearest > renderDistance || renderDistance <= detailDistance;
		if (!remove && !checkedOne) {
			checkedOne = true;//1024 lookups, one unit a frame is plenty
			remove = coveredByNear(unit);
		}
		if (remove) {
			pool.release(unit.mesh);
			it = units.erase(it);
		}
		else ++it;
	}

	// New builds, within the budget and without filling the workers' queue
	double now = nowSeconds();
	size_t maxJobs = workers.size() * jobsPerWorker;
	for (auto& pair : units) {
		if (jobs.size() >= maxJobs || nowSeconds() - start > budgetSeconds) break;
		FarUnit& unit = pair.second;
		if (!unit.dirty || unit.leaving || jobs.count(pair.first)) continue;
		if (now - unit.dirtyTime < settleSeconds) continue;
		startBuild(pair.first, unit);
	}
}
