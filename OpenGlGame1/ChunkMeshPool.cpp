#include "ChunkMeshPool.h"
#include <algorithm>
#include <cstddef>//offsetof
#include <iostream>

#include "GLFunctions.h"

namespace {
	const size_t initialVertices = 1 << 20;//16 MB, doubles when full (render distance 64 needs about 9 million)
	const size_t initialQuads = 1 << 16;
	const GLuint originAttribute = 5;//matches vertex_shader.glsl and depth_vertex.glsl

	// Room to grow, so rebuilding after an edit that adds a few faces usually fits in the same range
	GLsizei withSlack(GLsizei vertices) {
		GLsizei padded = vertices + vertices / 8;
		return (padded + 63) / 64 * 64;
	}
}

ChunkMeshPool::ChunkMeshPool() {
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &indexBuffer);
	glGenBuffers(1, &originBuffer);
	glGenBuffers(1, &commandBuffer);

	glBindVertexArray(vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);//remembered by the VAO

	// Chunk position, one per draw instead of one per vertex: divisor 1 means it advances once per instance,
	// and each draw's baseInstance says which entry it starts at, so draw i reads origins[i]
	glBindBuffer(GL_ARRAY_BUFFER, originBuffer);
	glVertexAttribPointer(originAttribute, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
	glVertexAttribDivisor(originAttribute, 1);
	glEnableVertexAttribArray(originAttribute);
	glBindVertexArray(0);

	growVertexBuffer(initialVertices);
	ensureIndices(initialQuads);
}

ChunkMeshPool::~ChunkMeshPool() {
	glDeleteVertexArrays(1, &vao);
	GLuint buffers[] = { vertexBuffer, indexBuffer, originBuffer, commandBuffer };
	glDeleteBuffers(4, buffers);
}

// ---------- allocation ----------

GLint ChunkMeshPool::allocate(GLsizei vertices) {
	for (;;) {
		// First fit, fine while the free list stays short (merging keeps it that way)
		for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it) {
			if (it->second < vertices) continue;
			GLint start = it->first;
			GLsizei remaining = it->second - vertices;
			freeRanges.erase(it);
			if (remaining > 0) freeRanges[start + vertices] = remaining;
			return start;
		}
		// Nothing big enough, grow (adds the new space to the end of the free list) and look again
		growVertexBuffer(std::max(vertexCapacity * 2, vertexCapacity + vertices));
	}
}

void ChunkMeshPool::free(GLint start, GLsizei vertices) {
	auto it = freeRanges.emplace(start, vertices).first;

	// Join with the range straight after it
	auto next = std::next(it);
	if (next != freeRanges.end() && it->first + it->second == next->first) {
		it->second += next->second;
		freeRanges.erase(next);
	}
	// and the one straight before it
	if (it != freeRanges.begin()) {
		auto previous = std::prev(it);
		if (previous->first + previous->second == it->first) {
			previous->second += it->second;
			freeRanges.erase(it);
		}
	}
}

void ChunkMeshPool::upload(MeshAllocation& allocation, const std::vector<PackedVertex>& vertices) {
	GLsizei count = static_cast<GLsizei>(vertices.size());
	if (count == 0) {
		release(allocation);//all air, nothing to draw
		return;
	}
	ensureIndices(count / 4);

	if (count > allocation.capacity) {
		release(allocation);
		allocation.capacity = withSlack(count);
		allocation.firstVertex = allocate(allocation.capacity);
	}
	usedVertices += count - allocation.vertexCount;
	allocation.vertexCount = count;

	// COPY_WRITE target so the upload doesn't disturb any VAO's bindings
	glBindBuffer(GL_COPY_WRITE_BUFFER, vertexBuffer);
	glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(allocation.firstVertex) * sizeof(PackedVertex),
		static_cast<GLsizeiptr>(count) * sizeof(PackedVertex), vertices.data());
}

void ChunkMeshPool::release(MeshAllocation& allocation) {
	if (allocation.capacity > 0) {
		free(allocation.firstVertex, allocation.capacity);
		usedVertices -= allocation.vertexCount;
	}
	allocation = MeshAllocation();
}

// ---------- buffers ----------

void ChunkMeshPool::growVertexBuffer(size_t minimumVertices) {
	size_t oldCapacity = vertexCapacity;
	GLuint newBuffer = 0;
	glGenBuffers(1, &newBuffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, newBuffer);
	glBufferData(GL_COPY_WRITE_BUFFER, minimumVertices * sizeof(PackedVertex), nullptr, GL_DYNAMIC_DRAW);

	// Existing meshes keep their offsets, so they're copied across on the GPU instead of re-uploaded
	if (vertexBuffer != 0) {
		glBindBuffer(GL_COPY_READ_BUFFER, vertexBuffer);
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, oldCapacity * sizeof(PackedVertex));
		glDeleteBuffers(1, &vertexBuffer);
		std::cout << "Chunk mesh buffer grown to " << (minimumVertices * sizeof(PackedVertex)) / (1024 * 1024) << " MB" << std::endl;
	}
	vertexBuffer = newBuffer;
	vertexCapacity = minimumVertices;
	setVertexAttributes();

	free(static_cast<GLint>(oldCapacity), static_cast<GLsizei>(minimumVertices - oldCapacity));
}

void ChunkMeshPool::ensureIndices(size_t quads) {
	if (quads <= quadCapacity) return;
	quadCapacity = std::max(quads, quadCapacity * 2);

	// Same as addQuad's corner order: two triangles 0,1,2 and 0,2,3 for every quad
	std::vector<GLuint> indices(quadCapacity * 6);
	for (size_t q = 0; q < quadCapacity; q++) {
		GLuint base = static_cast<GLuint>(q * 4);
		GLuint* out = &indices[q * 6];
		out[0] = base; out[1] = base + 1; out[2] = base + 2;
		out[3] = base; out[4] = base + 2; out[5] = base + 3;
	}
	glBindBuffer(GL_COPY_WRITE_BUFFER, indexBuffer);//same buffer name, so the VAO's binding stays valid
	glBufferData(GL_COPY_WRITE_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
}

void ChunkMeshPool::setVertexAttributes() {
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	//IPointer keeps them as integers in the shader (no conversion to float), locations match vertex_shader.glsl
	glVertexAttribIPointer(0, 3, GL_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, pos));
	glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, normal));
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, ao));
	glVertexAttribIPointer(3, 2, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, uv));
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, tile));
	for (int i = 0; i <= 4; i++) {
		glEnableVertexAttribArray(i);
	}
	glBindVertexArray(0);
}

size_t ChunkMeshPool::gpuBytes() const {
	return vertexCapacity * sizeof(PackedVertex) + quadCapacity * 6 * sizeof(GLuint);
}

size_t ChunkMeshPool::usedBytes() const {
	return usedVertices * sizeof(PackedVertex) + quadCapacity * 6 * sizeof(GLuint);
}

// ---------- drawing ----------

void ChunkMeshPool::bindForDrawing() const {
	glBindVertexArray(vao);
}

void ChunkMeshPool::draw(const std::vector<DrawCommand>& commands, const std::vector<glm::vec3>& origins) const {
	if (commands.empty()) return;

	// New storage every call (orphaning), so the driver doesn't wait for the GPU to finish with last frame's lists
	glBindBuffer(GL_ARRAY_BUFFER, originBuffer);
	glBufferData(GL_ARRAY_BUFFER, origins.size() * sizeof(glm::vec3), origins.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, commands.size() * sizeof(DrawCommand), commands.data(), GL_STREAM_DRAW);

	GLFunctions::multiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(commands.size()), 0);
}
