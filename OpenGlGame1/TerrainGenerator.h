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
	bool hasSnow(float x, float z, float height) const;//is ground this high at this position above the snow line
	int getSeed() const { return seed; }

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
	FastNoiseLite snowNoise;
	int seed;
};
