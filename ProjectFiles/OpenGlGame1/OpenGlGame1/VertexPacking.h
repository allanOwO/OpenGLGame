#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include<glm/gtc/packing.hpp>

struct PackedVertex {
    int16_t pos[3];       // 3 floats for position (12 bytes)
    uint32_t colour;     // Packed RGBA (4 bytes)
    uint16_t tex[2];    // 2 x 16-bit texture coordinates (4 bytes)
    int32_t normal;     // Packed normal using GL_INT_2_10_10_10_REV (4 bytes)

    PackedVertex() = default; // Ensure default construction works 
};

inline uint32_t packColor(const glm::vec3& colour) {
    uint8_t r = static_cast<uint8_t>(glm::clamp(colour.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(glm::clamp(colour.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(glm::clamp(colour.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = 255;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

inline void packTexCoord(const float tex[2], uint16_t outTex[2]) {
    // Assuming tex coords in [0,1]. Scale to [0, 65535].
    outTex[0] = static_cast<uint16_t>(glm::clamp(tex[0], 0.0f, 1.0f) * 65535.0f);
    outTex[1] = static_cast<uint16_t>(glm::clamp(tex[1], 0.0f, 1.0f) * 65535.0f);
}

inline int32_t packNormal(const glm::vec3& normal) {
    // Pack normal using GLM's packSnorm3x10_1x2. The last 2 bits can be unused (or used for something else).
    return glm::packSnorm3x10_1x2(glm::vec4(normal, 0)); 
} 
