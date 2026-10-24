#include "Chunk.h"
#include <iostream>
#include <algorithm>
#include <cstring>

#include "TerrainGenerator.h"
#include "BlockConstants.h"



Chunk::Chunk(glm::ivec3 position, const TerrainGenerator& terrain) : chunkPosition(position) {
	// Filled as one column first (simplest to fill), then split into sections and compressed
	std::vector<BlockType> flat(static_cast<size_t>(chunkSize) * chunkHeight * chunkSize, BlockType::AIR);

	for (int x = 0; x < chunkSize; x++) {
		for (int z = 0; z < chunkSize; z++) {

			float worldX = chunkPosition.x + x;
			float worldZ = chunkPosition.z + z;

			float height = terrain.getHeight(worldX, worldZ);

			int terrainHeight = static_cast<int>(height);
			for (int y = 0; y < chunkHeight; y++) {
				BlockType type;

				//dont add block type for air as its air by default
				if (y > terrainHeight) {
					break;
				}
				if (y == terrainHeight) {
					type = BlockType::GRASS;
				}
				else if (y >= terrainHeight - 3) {
					type = BlockType::DIRT;
				}
				else {
					type = BlockType::STONE;
				}

				flat[flatIndex(x, y, z)] = type;
			}
		}
	}
	buildSections(flat);
}

Chunk::Chunk(glm::ivec3 position, const std::vector<BlockType>& savedBlocks)
	: chunkPosition(position), modified(true) {
	// Still modified, if it unloads again it has to be stored again
	buildSections(savedBlocks);
}

void Chunk::buildSections(const std::vector<BlockType>& flat) {
	// The flat column and a section use the same y, z, x order, so each section is one contiguous slice
	for (int s = 0; s < sectionCount; s++) {
		ChunkSection& section = sections[s];
		const BlockType* start = &flat[flatIndex(0, s * ChunkSection::size, 0)];
		section.blocks.assign(start, start + ChunkSection::volume);
		section.compress();
	}
}

std::vector<BlockType> Chunk::flatBlocks() const {
	std::vector<BlockType> flat(static_cast<size_t>(chunkSize) * chunkHeight * chunkSize);
	for (int s = 0; s < sectionCount; s++) {
		const ChunkSection& section = sections[s];
		BlockType* out = &flat[flatIndex(0, s * ChunkSection::size, 0)];
		if (section.isUniform()) std::fill(out, out + ChunkSection::volume, section.fill);
		else std::copy(section.blocks.begin(), section.blocks.end(), out);
	}
	return flat;
}

// ---------- sections ----------

bool ChunkSection::set(int x, int y, int z, BlockType type) {
	if (get(x, y, z) == type) return false;
	if (blocks.empty()) blocks.assign(volume, fill);//first different block, now it needs the full array
	blocks[index(x, y, z)] = type;
	return true;
}

void ChunkSection::compress() {
	if (blocks.empty()) return;
	BlockType first = blocks[0];
	for (BlockType b : blocks) {
		if (b != first) return;//mixed, keep the array
	}
	fill = first;
	blocks.clear();
	blocks.shrink_to_fit();//clear alone keeps the memory
}

// ---------- meshing input ----------

ChunkMeshInput Chunk::makeMeshInput(int sectionIndex, const Chunk* const neighbours[8]) const {
	constexpr int P = ChunkMeshInput::paddedSize;
	constexpr int S = ChunkSection::size;
	ChunkMeshInput input;
	input.blocks.resize(static_cast<size_t>(P) * P * P);

	// Which column a padded x/z coordinate (-1 to 16) falls in, -1/0/+1 on each axis
	auto columnAt = [&](int dx, int dz) -> const Chunk* {
		if (dx == 0 && dz == 0) return this;
		if (dz == 0) return dx > 0 ? neighbours[0] : neighbours[1];
		if (dx == 0) return dz > 0 ? neighbours[2] : neighbours[3];
		if (dx > 0) return dz > 0 ? neighbours[4] : neighbours[5];
		return dz > 0 ? neighbours[6] : neighbours[7];
	};

	for (int py = -1; py <= S; py++) {
		int y = sectionIndex * S + py;
		for (int pz = -1; pz <= S; pz++) {
			BlockType* row = &input.blocks[(static_cast<size_t>(py + 1) * P + (pz + 1)) * P];
			// Above the world is open sky. Below it counts as solid, so the bottom of the world gets no faces nobody can see
			if (y < 0) { std::fill(row, row + P, BlockType::STONE); continue; }
			if (y >= chunkHeight) { std::fill(row, row + P, BlockType::AIR); continue; }

			int dz = pz < 0 ? -1 : (pz >= S ? 1 : 0);
			int localZ = pz - dz * S;
			int localY = y % S;
			for (int dx = -1; dx <= 1; dx++) {
				const Chunk* column = columnAt(dx, dz);
				// x range this column covers in the padded row: just the edge cell for neighbours, 16 for the middle
				int first = dx < 0 ? -1 : (dx == 0 ? 0 : S);
				int count = dx == 0 ? S : 1;
				BlockType* out = row + (first + 1);
				if (!column) { std::fill(out, out + count, BlockType::AIR); continue; }//not loaded

				const ChunkSection& section = column->sections[y / S];
				int localX = first - dx * S;
				if (section.isUniform()) {
					std::fill(out, out + count, section.fill);
				}
				else {
					// x is innermost in both, so the row is one copy
					std::memcpy(out, &section.blocks[ChunkSection::index(localX, localY, localZ)], count * sizeof(BlockType));
				}
			}
		}
	}
	return input;
}

bool Chunk::sectionHasNoFaces(int sectionIndex, const Chunk* const neighbours[8]) const {
	const ChunkSection& section = sections[sectionIndex];
	if (section.isEmpty()) return true;//faces belong to solid blocks, air has none
	if (!section.isUniform()) return false;

	// Solid all the way through: faces only appear where a neighbouring section has an air block against it
	auto solidSection = [](const Chunk* column, int index) {
		if (!column) return false;//not loaded counts as air
		const ChunkSection& other = column->sections[index];
		return other.isUniform() && other.fill != BlockType::AIR;
	};
	bool below = sectionIndex == 0 || solidSection(this, sectionIndex - 1);//below the world counts as solid
	bool above = sectionIndex + 1 < sectionCount && solidSection(this, sectionIndex + 1);
	return below && above
		&& solidSection(neighbours[0], sectionIndex) && solidSection(neighbours[1], sectionIndex)
		&& solidSection(neighbours[2], sectionIndex) && solidSection(neighbours[3], sectionIndex);
}

// ---------- greedy meshing ----------

// Face index (see BlockFace) from the axis a face points along (0 = x, 1 = y, 2 = z) and which way (-1 or +1)
static int faceIndex(int axis, int direction) {
	switch (axis) {
	case 0: return direction < 0 ? 2 : 3; // Left, Right
	case 1: return direction < 0 ? 5 : 4; // Bottom, Top
	default: return direction < 0 ? 0 : 1; // Front, Back
	}
}

// The padded input as bits: rows[y + 1][z + 1] has bit x + 1 set when that block is solid
// One 32 bit row covers the whole x range (-1 to 16), so checking a block is a shift instead of an array lookup,
// and whole rows can be compared at once to find which slices have any faces at all
struct SolidRows {
	static constexpr int P = ChunkMeshInput::paddedSize;
	static constexpr uint32_t inner = 0x1FFFE;//bits for x = 0 to 15, leaving out the border on each side
	uint32_t rows[P][P];

	explicit SolidRows(const ChunkMeshInput& input) {
		for (int y = -1; y <= ChunkSection::size; y++) {
			for (int z = -1; z <= ChunkSection::size; z++) {
				uint32_t bits = 0;
				for (int x = -1; x <= ChunkSection::size; x++) {
					if (input.get(x, y, z) != BlockType::AIR) bits |= 1u << (x + 1);
				}
				rows[y + 1][z + 1] = bits;
			}
		}
	}

	bool operator()(const glm::ivec3& p) const { return (rows[p.y + 1][p.z + 1] >> (p.x + 1)) & 1u; }

	// Bit i set = slice i along this axis has at least one face pointing this direction
	// A face is a solid block with air next to it, which for a whole row is just `row & ~neighbourRow`
	uint32_t slicesWithFaces(int axis, int direction) const {
		const int S = ChunkSection::size;
		uint32_t slices = 0;
		if (axis == 0) {
			// Neighbour along x is in the same row, one bit over
			uint32_t any = 0;
			for (int y = 0; y < S; y++) {
				for (int z = 0; z < S; z++) {
					uint32_t row = rows[y + 1][z + 1];
					any |= row & ~(direction > 0 ? row >> 1 : row << 1);
				}
			}
			slices = (any & inner) >> 1;//bit x + 1 -> slice x
		}
		else {
			for (int slice = 0; slice < S; slice++) {
				uint32_t any = 0;
				for (int i = 0; i < S && !(any & inner); i++) {
					// axis 1: slice is y, rows vary in z. axis 2: slice is z, rows vary in y
					int y = axis == 1 ? slice : i, z = axis == 1 ? i : slice;
					int ny = axis == 1 ? y + direction : y, nz = axis == 2 ? z + direction : z;
					any |= rows[y + 1][z + 1] & ~rows[ny + 1][nz + 1];
				}
				if (any & inner) slices |= 1u << slice;
			}
		}
		return slices;
	}
};

// Ambient occlusion for one corner of a face: 3 = open, 0 = fully tucked into a corner
// side1/side2 are the blocks along each edge next to the corner, corner is the one diagonally across
static uint32_t vertexAO(bool side1, bool side2, bool corner) {
	if (side1 && side2) return 0;// both edges blocked, the diagonal can't let any more light in
	return 3 - (side1 + side2 + corner);
}

// Texture coordinates in blocks for a vertex of a face
// Picked so every face's texture is upright (grass sides have grass at the top) and repeats once per block
// Offsets like chunkHeight - y flip the direction while keeping the numbers positive for uint16
static glm::ivec2 faceUV(int face, const glm::ivec3& p) {
	switch (face) {
	case 0:  return { p.x, Chunk::chunkHeight - p.y };                   // Front (-Z)
	case 1:  return { Chunk::chunkSize - p.x, Chunk::chunkHeight - p.y }; // Back (+Z)
	case 2:  return { Chunk::chunkSize - p.z, Chunk::chunkHeight - p.y }; // Left (-X)
	case 3:  return { p.z, Chunk::chunkHeight - p.y };                   // Right (+X)
	case 4:  return { p.x, Chunk::chunkSize - p.z };                     // Top
	default: return { p.x, p.z };                                        // Bottom
	}
}

// Mask cell layout: bit 16 = face present, bits 8-15 = atlas tile, bits 0-7 = AO for the 4 corners (2 bits each)
// Two cells can merge only if the whole value matches, so same texture AND same corner shading
static uint32_t makeCell(uint32_t tile, uint32_t ao00, uint32_t ao10, uint32_t ao11, uint32_t ao01) {
	return (1u << 16) | (tile << 8) | ao00 | (ao10 << 2) | (ao11 << 4) | (ao01 << 6);
}

// Adds one rectangle covering several blocks
// origin is its corner on the face plane, du/dv are its full width and height along the u/v axes
static void addQuad(MeshData& mesh, const glm::ivec3& origin, const glm::ivec3& du, const glm::ivec3& dv,
	int direction, int face, uint32_t cell) {

	uint16_t tile = static_cast<uint16_t>((cell >> 8) & 0xFF);
	uint8_t ao[4] = {// corners in order (-u,-v) (+u,-v) (+u,+v) (-u,+v)
		static_cast<uint8_t>(cell & 3), static_cast<uint8_t>((cell >> 2) & 3),
		static_cast<uint8_t>((cell >> 4) & 3), static_cast<uint8_t>((cell >> 6) & 3)
	};
	glm::ivec3 corners[4] = { origin, origin + du, origin + du + dv, origin + dv };

	// cross(u, v) points along +axis, so going round the corners in order is counter clockwise (front facing)
	// seen from the + side; faces pointing the - way go round the other way
	const int forward[4] = { 0, 1, 2, 3 };
	const int backward[4] = { 0, 3, 2, 1 };
	const int* order = direction > 0 ? forward : backward;

	// Every quad is drawn with the same shared indices 0,1,2 0,2,3 (see ChunkMeshPool), which split it along
	// the first and third vertex. Either diagonal works, but the GPU blends AO across each triangle,
	// so split along the brighter one: starting one corner later moves the split to the other diagonal
	// (going round the same corners from a different start keeps the winding, so the face still points out)
	int start = (ao[0] + ao[2] < ao[1] + ao[3]) ? 1 : 0;
	for (int i = 0; i < 4; i++) {
		int c = order[(i + start) % 4];
		glm::ivec2 uv = faceUV(face, corners[c]);
		PackedVertex vertex{};
		vertex.pos[0] = static_cast<int16_t>(corners[c].x);
		vertex.pos[1] = static_cast<int16_t>(corners[c].y);
		vertex.pos[2] = static_cast<int16_t>(corners[c].z);
		vertex.normal = static_cast<uint8_t>(face);
		vertex.ao = ao[c];
		vertex.uv[0] = static_cast<uint16_t>(uv.x);
		vertex.uv[1] = static_cast<uint16_t>(uv.y);
		vertex.tile = tile;
		mesh.vertices.push_back(vertex);
	}
}

MeshData Chunk::buildMesh(const ChunkMeshInput& input) {
	MeshData mesh;
	SolidRows solid(input);

	// Area to mesh: one section
	const int size[3] = { ChunkSection::size, ChunkSection::size, ChunkSection::size };
	std::vector<uint32_t> mask;

	for (int axis = 0; axis < 3; axis++) {
		// u and v are the two directions across a face, chosen so cross(u, v) points along +axis
		int u = (axis + 1) % 3;
		int v = (axis + 2) % 3;
		glm::ivec3 uStep(0), vStep(0);
		uStep[u] = 1;
		vStep[v] = 1;
		mask.assign(static_cast<size_t>(size[u]) * size[v], 0);

		for (int direction = -1; direction <= 1; direction += 2) {
			int face = faceIndex(axis, direction);

			// One 2D slice of the chunk at a time, skipping slices with nothing to draw (all sky or all rock)
			uint32_t slicesToMesh = solid.slicesWithFaces(axis, direction);
			for (int slice = 0; slice < size[axis]; slice++) {
				if (!(slicesToMesh & (1u << slice))) continue;

				// 1. Build the mask: what face (if any) each cell of this slice shows
				for (int b = 0; b < size[v]; b++) {
					for (int a = 0; a < size[u]; a++) {
						uint32_t& cell = mask[a + b * size[u]];
						cell = 0;

						glm::ivec3 pos(0);
						pos[axis] = slice;
						pos[u] = a;
						pos[v] = b;
						if (!solid(pos)) continue;

						glm::ivec3 front = pos;//the block the face looks into
						front[axis] += direction;
						if (solid(front)) continue;//face is hidden

						// Blocks around the face's 4 corners, checked in the layer the face looks into
						bool uBehind = solid(front - uStep), uAhead = solid(front + uStep);
						bool vBehind = solid(front - vStep), vAhead = solid(front + vStep);
						uint32_t ao00 = vertexAO(uBehind, vBehind, solid(front - uStep - vStep));
						uint32_t ao10 = vertexAO(uAhead, vBehind, solid(front + uStep - vStep));
						uint32_t ao11 = vertexAO(uAhead, vAhead, solid(front + uStep + vStep));
						uint32_t ao01 = vertexAO(uBehind, vAhead, solid(front - uStep + vStep));

						BlockType type = input.get(pos.x, pos.y, pos.z);
						uint32_t tile = static_cast<uint32_t>(getBlockTile(type, static_cast<BlockFace>(face)));
						cell = makeCell(tile, ao00, ao10, ao11, ao01);
					}
				}

				// 2. Greedy merge: grow each unclaimed cell into the biggest rectangle of identical cells
				for (int b = 0; b < size[v]; b++) {
					for (int a = 0; a < size[u];) {
						uint32_t cell = mask[a + b * size[u]];
						if (cell == 0) { a++; continue; }

						// Grow along u while the cells match
						int width = 1;
						while (a + width < size[u] && mask[a + width + b * size[u]] == cell) {
							width++;
						}

						// Grow along v while every cell in the next row matches
						int height = 1;
						bool rowMatches = true;
						while (b + height < size[v] && rowMatches) {
							for (int k = 0; k < width; k++) {
								if (mask[a + k + (b + height) * size[u]] != cell) { rowMatches = false; break; }
							}
							if (rowMatches) height++;
						}

						// Faces pointing + sit on the far side of the block
						glm::ivec3 origin(0);
						origin[axis] = slice + (direction > 0 ? 1 : 0);
						origin[u] = a;
						origin[v] = b;
						addQuad(mesh, origin, uStep * width, vStep * height, direction, face, cell);

						// Claim the cells so they aren't used again
						for (int h = 0; h < height; h++) {
							for (int k = 0; k < width; k++) {
								mask[a + k + (b + h) * size[u]] = 0;
							}
						}
						a += width;
					}
				}
			}
		}
	}
	return mesh;
}

bool Chunk::setBlock(int x, int y, int z, BlockType type) {
	if (x < 0 || x >= chunkSize || y < 0 || y >= chunkHeight || z < 0 || z >= chunkSize) return false;//if outside chunk

	ChunkSection& section = sections[y / ChunkSection::size];
	if (!section.set(x, y % ChunkSection::size, z, type)) return false;
	section.rebuildNeeded = true;
	modified = true;
	return true;
}

BlockType Chunk::getBlock(int x, int y, int z) const {
	return sections[y / ChunkSection::size].get(x, y % ChunkSection::size, z);
}
