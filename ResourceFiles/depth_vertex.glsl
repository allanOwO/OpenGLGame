#version 330 core
layout(location = 0) in ivec3 aPos;
layout(location = 5) in vec3 aChunkOrigin;//per draw, see ChunkMeshPool

uniform mat4 lightSpaceMatrix;

void main(){

	gl_Position = lightSpaceMatrix * vec4(vec3(aPos) + aChunkOrigin, 1.0);
}