#include "TerrainGenerator.h"
#include <glm/glm.hpp>
#include <cmath>
#include "Chunk.h"//for chunkHeight


// Frequency is 1 / (rough size of a feature in blocks)
TerrainGenerator::TerrainGenerator(int seed) : seed(seed) {
	// Continents: features thousands of blocks across. A few octaves so coastlines wiggle instead of being smooth blobs
	continentNoise.SetSeed(seed);
	continentNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	continentNoise.SetFrequency(0.0007f);
	continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
	continentNoise.SetFractalOctaves(4);

	// Where mountain ranges are, around a thousand blocks apart. Smooth, it only says how mountainous an area is
	mountainMaskNoise.SetSeed(seed + 1);
	mountainMaskNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	mountainMaskNoise.SetFrequency(0.0012f);
	mountainMaskNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
	mountainMaskNoise.SetFractalOctaves(2);

	// The peaks themselves. Ridged noise is 1 - |noise|, so where normal noise crosses zero you get a sharp crest
	// instead of a rounded hill, which is what makes ridgelines and valleys
	ridgeNoise.SetSeed(seed + 2);
	ridgeNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	ridgeNoise.SetFrequency(0.004f);
	ridgeNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
	ridgeNoise.SetFractalOctaves(5);

	// Local hills and bumps, about the same scale and octaves the old generator used
	detailNoise.SetSeed(seed + 3);
	detailNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	detailNoise.SetFrequency(0.009f);
	detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
	detailNoise.SetFractalOctaves(4);
	detailNoise.SetFractalGain(0.6f);

	// Pushes the sample position around a little before detail and ridges are read, so they don't look grid aligned
	warpNoise.SetSeed(seed + 4);
	warpNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	warpNoise.SetFrequency(0.006f);
}

float TerrainGenerator::getHeight(float x, float z) const {
	float continentalness = continentNoise.GetNoise(x, z);

	float height = continentHeight(continentalness)
		+ mountainHeight(x, z, continentalness)
		+ detailHeight(x, z);

	return glm::clamp(height, 0.0f, static_cast<float>(Chunk::chunkHeight - 1));
}

// Continentalness (-1 to 1) -> ground height, as a curve through hand picked points
// A curve instead of a formula makes it easy to tune: each point says "at this much land, the ground is this high"
// The steep part around -0.15 to 0 is the coast, so beaches are narrow and oceans drop off into deep water
float TerrainGenerator::continentHeight(float continentalness) const {
	struct Point { float continentalness, height; };//height relative to sea level
	static const Point curve[] = {
		{ -1.00f, -45.0f },//deepest ocean
		{ -0.45f, -35.0f },
		{ -0.25f, -16.0f },//shallow sea
		{ -0.12f,  -3.0f },//coast
		{  0.00f,   2.0f },//lowland just above the water
		{  0.30f,  10.0f },
		{  1.00f,  24.0f },//high inland ground
	};
	const int count = sizeof(curve) / sizeof(curve[0]);

	if (continentalness <= curve[0].continentalness) return seaLevel + curve[0].height;
	for (int i = 1; i < count; i++) {
		if (continentalness <= curve[i].continentalness) {
			const Point& a = curve[i - 1];
			const Point& b = curve[i];
			float t = (continentalness - a.continentalness) / (b.continentalness - a.continentalness);
			// smoothstep instead of straight lines, so the slope doesn't kink at each point
			return seaLevel + glm::mix(a.height, b.height, t * t * (3.0f - 2.0f * t));
		}
	}
	return seaLevel + curve[count - 1].height;
}

// Extra height from mountains, 0 outside mountain ranges
float TerrainGenerator::mountainHeight(float x, float z, float continentalness) const {
	// How mountainous this area is, 0 to 1. Only on land: fading in from the coast keeps peaks out of the sea
	float mask = glm::smoothstep(-0.05f, 0.3f, mountainMaskNoise.GetNoise(x, z));
	mask *= glm::smoothstep(-0.05f, 0.15f, continentalness);
	if (mask <= 0.0f) return 0.0f;//skips the 5 octave ridge noise for most of the world

	float warpX = warpNoise.GetNoise(x, z) * 30.0f;
	float warpZ = warpNoise.GetNoise(x + 1000.0f, z + 1000.0f) * 30.0f;
	float ridges = (ridgeNoise.GetNoise(x + warpX, z + warpZ) + 1.0f) * 0.5f;//0 to 1, 1 on the crests
	ridges = std::pow(ridges, 1.5f);//sharper peaks, wider valleys

	// The mask scales both a raised base and the ridges, so ranges rise out of foothills instead of popping up
	// Top of the tallest peak: sea level + 24 (land) + 25 + 140 + detail, just under the chunk height of 256
	float uplift = 25.0f;
	float peakHeight = 140.0f;
	return mask * (uplift + ridges * peakHeight);
}

// Small hills everywhere, roughly -8 to +8 blocks
float TerrainGenerator::detailHeight(float x, float z) const {
	float warpX = warpNoise.GetNoise(x + 500.0f, z + 500.0f) * 15.0f;
	float warpZ = warpNoise.GetNoise(x + 1500.0f, z + 1500.0f) * 15.0f;
	return detailNoise.GetNoise(x + warpX, z + warpZ) * 8.0f;
}
