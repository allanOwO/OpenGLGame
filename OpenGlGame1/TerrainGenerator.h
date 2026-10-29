#pragma once
#include <FastNoiseLite.h>

// Turns a world x/z position into a terrain height
// Everything here is const after construction, so chunk generation threads can share one instance
//
// The height is three layers of noise added together, each at its own scale:
//   continents - very large, decides ocean vs land and the base height of the ground
//   mountains  - large, decides where mountain ranges are, then ridged noise shapes the peaks inside them
//   detail     - small, the bumps and hills everywhere (the Perlin noise the old generator was built on)
class TerrainGenerator
{
public:
	explicit TerrainGenerator(int seed);

	float getHeight(float x, float z) const;//height in chunk local y (0 to Chunk::chunkHeight - 1)
	int getSeed() const { return seed; }

	// Chunk local y of the sea surface. Chunks sit baseTerrainHeight below world y = 0, so this is world y = 0
	static constexpr float seaLevel = 64.0f;

private:
	float continentHeight(float continentalness) const;
	float mountainHeight(float x, float z, float continentalness) const;
	float detailHeight(float x, float z) const;

	// One noise object per layer, each with its own seed, so the layers don't line up with each other
	// (sampling one noise at different scales leaves the big and small features correlated)
	FastNoiseLite continentNoise;
	FastNoiseLite mountainMaskNoise;
	FastNoiseLite ridgeNoise;
	FastNoiseLite detailNoise;
	FastNoiseLite warpNoise;
	int seed;
};
