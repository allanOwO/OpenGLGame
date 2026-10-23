#include "TerrainGenerator.h"
#include <glm/glm.hpp>
#include <cmath>
#include "Chunk.h"//for chunkHeight


TerrainGenerator::TerrainGenerator(int seed) : seed(seed) {
	//create noise for world gen
	noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	noise.SetFrequency(0.03f); // Lower frequency for smoother terrain
	noise.SetSeed(seed);
}

float TerrainGenerator::getHeight(float x, float z) const {
	float biomeValue = getBiomeNoise(x, z);
	float warpedHeight = getWarpedHeight(x, z, biomeValue);
	return remapHeight(warpedHeight, biomeValue);
}

// Low-frequency noise to determine biome type
float TerrainGenerator::getBiomeNoise(float x, float z) const {
	float biomeScale = 0.002f; // Very low frequency for large biome areas
	float biomeNoise = noise.GetNoise(x * biomeScale, z * biomeScale);
	return (biomeNoise + 1.0f) / 2.0f; // Normalize to 0-1
}

// How much of each biome applies, smoothstep keeps the blend continuous so there are no cliffs at biome edges
static float hillsWeight(float biomeValue) { return glm::smoothstep(0.25f, 0.45f, biomeValue); }
static float mountainsWeight(float biomeValue) { return glm::smoothstep(0.55f, 0.75f, biomeValue); }

// pow that keeps the sign, so values under "flatness" curve down instead of mirroring back up
static float signedPow(float value, float exponent) {
	return value < 0.0f ? -pow(-value, exponent) : pow(value, exponent);
}

//takes 0 to 1 noise value and assigns height in y
float TerrainGenerator::remapHeight(float noiseValue, float biomeValue) const {
	float minHeight = 32.0f;  // Base height
	float maxHeight = 150.0f; // Max height (can increase for taller mountains)

	// Define height functions for each biome
	auto plainsHeight = [](float nv) {
		float flatness = 0.8f; // Keep plains flattish
		return flatness * nv;
		};

	auto hillsHeight = [](float nv) {
		float flatness = 0.5f;
		float steepness = 2.0f;
		return flatness + (1.0f - flatness) * signedPow((nv - flatness) / (1.0f - flatness), steepness);
		};

	auto mountainsHeight = [](float nv) {
		float flatness = 0.3f; // Less flat area for mountains
		float steepness = 3.0f; // Steeper slopes
		return flatness + (1.0f - flatness) * signedPow((nv - flatness) / (1.0f - flatness), steepness);
		};

	// Blend plains -> hills -> mountains
	float hills = hillsWeight(biomeValue);
	float mountains = mountainsWeight(biomeValue);
	float height = glm::mix(plainsHeight(noiseValue), hillsHeight(noiseValue), hills);
	height = glm::mix(height, mountainsHeight(noiseValue), mountains);

	// Scale height based on biome, mountains top out just under the chunk height
	float mountainScale = (Chunk::chunkHeight - 1 - minHeight) / maxHeight;
	float scale = glm::mix(0.2f, 0.8f, hills);
	scale = glm::mix(scale, mountainScale, mountains);

	float finalHeight = minHeight + height * (maxHeight * scale);
	return glm::clamp(finalHeight, 0.0f, static_cast<float>(Chunk::chunkHeight - 1));
}

// Layered noise (fractal brownian motion) normalised to 0-1
static float fbm(const FastNoiseLite& noise, float x, float z, float frequency, float persistence, int octaves) {
	float height = 0.0f;
	float amplitude = 0.9f;
	float lacunarity = 2.0f;
	float totalAmplitude = 0.0f;

	for (int i = 0; i < octaves; i++) {
		height += noise.GetNoise(x * frequency, z * frequency) * amplitude;
		totalAmplitude += amplitude;
		amplitude *= persistence;
		frequency *= lacunarity;
	}

	return (height + totalAmplitude) / (2.0f * totalAmplitude);
}

// Domain warped height for more variation
float TerrainGenerator::getWarpedHeight(float x, float z, float biomeValue) const {
	float warpScale = 0.2f;
	float warpX = noise.GetNoise(x * warpScale + 10.0f, z * warpScale + 10.0f) * 15.0f;
	float warpZ = noise.GetNoise(x * warpScale + 20.0f, z * warpScale + 20.0f) * 15.0f;
	float warpedX = x + warpX;
	float warpedZ = z + warpZ;

	// Mountains use lower frequency, more octaves (jaggedness) and exaggerated peaks
	// Both are worked out and blended in the transition so the terrain doesn't jump
	float mountains = mountainsWeight(biomeValue);
	float lowland = 0.0f, highland = 0.0f;
	if (mountains < 1.0f) {
		lowland = fbm(noise, warpedX, warpedZ, 0.3f, 0.6f, 4);
	}
	if (mountains > 0.0f) {
		highland = pow(fbm(noise, warpedX, warpedZ, 0.08f, 0.7f, 5), 0.6f);
	}

	return glm::mix(lowland, highland, mountains);
}
