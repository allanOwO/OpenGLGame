#pragma once
#include <FastNoiseLite.h>

// Turns a world x/z position into a terrain height
// Everything here is const after construction, so chunk generation threads can share one instance
class TerrainGenerator
{
public:
	explicit TerrainGenerator(int seed);

	float getHeight(float x, float z) const;//height in chunk local y (0 to Chunk::chunkHeight - 1)
	int getSeed() const { return seed; }

private:
	float getBiomeNoise(float x, float z) const;
	float remapHeight(float noiseValue, float biomeValue) const;
	float getWarpedHeight(float x, float z, float biomeValue) const;

	FastNoiseLite noise;
	int seed;
};
