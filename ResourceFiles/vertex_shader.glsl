#version 330 core
// Must match PackedVertex in VertexPacking.h and the attribute setup in Chunk::initializeBuffers
layout (location = 0) in ivec3 aPos;
layout (location = 1) in uint aNormal;    // face index 0-5 (Front, Back, Left, Right, Top, Bottom)
layout (location = 2) in uint aAO;        // 0 = darkest corner, 3 = no occlusion
layout (location = 3) in uvec2 aTexCoord; // in blocks, the tile repeats every 1.0
layout (location = 4) in uint aTile;      // atlas tile index

out float AO;//corner brightness, blended across the face by the GPU
out vec2 TexCoord;//in blocks
flat out vec2 TileOrigin;//flat = same for the whole face, no blending
out vec3 FragPos;//world space pos
out vec3 Normal;//normal vector
out vec4 FragPosLightSpace;//output the shadwos

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

const vec3 faceNormals[6] = vec3[6](
    vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0),
    vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0)
);

// Brightness for each AO level, the gaps between them set how strong the effect looks
const float aoBrightness[4] = float[4](0.45, 0.65, 0.82, 1.0);

// Atlas is atlasTiles x atlasTiles, must match atlasTiles in BlockConstants.h
const uint atlasTiles = 4u;

void main()
{
    vec3 pos = vec3(aPos);

    gl_Position = projection * view * model * vec4(pos, 1.0);
    FragPos = vec3(model * vec4(pos, 1.0)); // World-space position

    Normal = faceNormals[int(aNormal)]; // model is only ever a translation, so normals are unchanged
    AO = aoBrightness[int(aAO)];

    TexCoord = vec2(aTexCoord);
    TileOrigin = vec2(float(aTile % atlasTiles), float(aTile / atlasTiles)) / float(atlasTiles);

    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos,1.0);
}
