#version 330 core
layout(location = 0) in ivec3 aPos;
layout(location = 5) in vec3 aChunkOrigin;//per draw, see ChunkMeshPool
layout(location = 6) in uint aPlace;//section within the region, same as vertex_shader.glsl

uniform mat4 lightSpaceMatrix;

void main(){
	vec3 place = vec3(float(aPlace & 7u), float((aPlace >> 6) & 15u), float((aPlace >> 3) & 7u)) * 16.0;
	gl_Position = lightSpaceMatrix * vec4(vec3(aPos) + place + aChunkOrigin, 1.0);
}