#include "ChunkMeshPool.h"
#include <algorithm>
#include <cstddef>//offsetof
#include <iostream>

#include "GLFunctions.h"

namespace {
	// 64 MB each. Render distance 200 with LOD fits in about 3, without LOD in about 22
	// Smaller pages waste less at the end of each, but every page is its own draw call
	const GLsizei pageVertices = 1 << 22;
	const size_t initialQuads = 1 << 16;
	const GLuint originAttribute = 5;//matches vertex_shader.glsl and depth_vertex.glsl
	const GLuint placeAttribute = 6;

	// Room to grow, so rebuilding after an edit that adds a few faces usually fits in the same range
	GLsizei withSlack(GLsizei vertices) {
		GLsizei padded = vertices + vertices / 8;
		return (padded + 63) / 64 * 64;
	}
}

ChunkMeshPool::ChunkMeshPool() {
	glGenBuffers(1, &indexBuffer);
	glGenBuffers(1, &originBuffer);
	glGenBuffers(1, &commandBuffer);
	ensureIndices(initialQuads);
	addPage(pageVertices);
}

ChunkMeshPool::~ChunkMeshPool() {
	for (Page& page : pages) {
		glDeleteVertexArrays(1, &page.vao);
		glDeleteBuffers(1, &page.buffer);
	}
	GLuint buffers[] = { indexBuffer, originBuffer, commandBuffer };
	glDeleteBuffers(3, buffers);
}

// ---------- allocation ----------

MeshAllocation ChunkMeshPool::allocate(GLsizei vertices) {
	for (;;) {
		// First fit, fine while the free lists stay short (merging keeps them that way)
		for (size_t p = 0; p < pages.size(); p++) {
			std::map<GLint, GLsizei>& freeRanges = pages[p].freeRanges;
			for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it) {
				if (it->second < vertices) continue;
				MeshAllocation allocation;
				allocation.page = static_cast<int>(p);
				allocation.firstVertex = it->first;
				allocation.capacity = vertices;
				GLsizei remaining = it->second - vertices;
				freeRanges.erase(it);
				if (remaining > 0) freeRanges[allocation.firstVertex + vertices] = remaining;
				return allocation;
			}
		}
		// Nothing big enough anywhere, add a page (a bigger one in the unlikely case a mesh doesn't fit a normal one)
		addPage(std::max(pageVertices, vertices));
	}
}

void ChunkMeshPool::free(int page, GLint start, GLsizei vertices) {
	std::map<GLint, GLsizei>& freeRanges = pages[page].freeRanges;
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

void ChunkMeshPool::upload(MeshAllocation& allocation, const std::vector<PackedVertex>& vertices, const int groupCounts[faceGroupCount]) {
	GLsizei count = static_cast<GLsizei>(vertices.size());
	if (count == 0) {
		release(allocation);//all air, nothing to draw
		return;
	}
	ensureIndices(count / 4);

	if (count > allocation.capacity) {
		release(allocation);
		allocation = allocate(withSlack(count));
	}
	usedVertices += count - allocation.vertexCount;
	allocation.vertexCount = count;
	for (int g = 0; g < faceGroupCount; g++) allocation.groupCounts[g] = groupCounts[g];

	// COPY_WRITE target so the upload doesn't disturb any VAO's bindings
	glBindBuffer(GL_COPY_WRITE_BUFFER, pages[allocation.page].buffer);
	glBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(allocation.firstVertex) * sizeof(PackedVertex),
		static_cast<GLsizeiptr>(count) * sizeof(PackedVertex), vertices.data());
}

void ChunkMeshPool::combine(MeshAllocation& target, const std::vector<MeshRange>& parts, const GLsizei groupCounts[faceGroupCount]) {
	GLsizei total = 0;
	for (const MeshRange& part : parts) total += part.count;
	if (total == 0) {
		release(target);
		return;
	}
	ensureIndices(total / 4);

	// A new range, no slack: merged meshes are rebuilt whole, never edited in place
	MeshAllocation combined = allocate(total);
	combined.vertexCount = total;
	for (int g = 0; g < faceGroupCount; g++) combined.groupCounts[g] = groupCounts[g];

	// Reading and writing the same buffer is fine as long as the ranges don't overlap, and a fresh range can't
	// Parts that already sit next to each other are copied together
	glBindBuffer(GL_COPY_WRITE_BUFFER, pages[combined.page].buffer);
	GLint written = combined.firstVertex;
	MeshRange run{ 0, 0, 0 };
	auto copyRun = [&]() {
		if (run.count == 0) return;
		glBindBuffer(GL_COPY_READ_BUFFER, pages[run.page].buffer);
		glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
			static_cast<GLintptr>(run.first) * sizeof(PackedVertex), static_cast<GLintptr>(written) * sizeof(PackedVertex),
			static_cast<GLsizeiptr>(run.count) * sizeof(PackedVertex));
		written += run.count;
	};
	for (const MeshRange& part : parts) {
		if (part.count == 0) continue;
		if (run.count > 0 && run.page == part.page && run.first + run.count == part.first) {
			run.count += part.count;
			continue;
		}
		copyRun();
		run = part;
	}
	copyRun();

	release(target);//after copying, parts can be slices of the old mesh
	usedVertices += total;
	target = combined;
}

void ChunkMeshPool::release(MeshAllocation& allocation) {
	if (allocation.capacity > 0) {
		free(allocation.page, allocation.firstVertex, allocation.capacity);
		usedVertices -= allocation.vertexCount;
	}
	allocation = MeshAllocation();
}

// ---------- buffers ----------

void ChunkMeshPool::addPage(GLsizei vertices) {
	Page page;
	page.capacity = vertices;
	glGenBuffers(1, &page.buffer);
	glBindBuffer(GL_COPY_WRITE_BUFFER, page.buffer);
	glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(vertices) * sizeof(PackedVertex), nullptr, GL_DYNAMIC_DRAW);

	// Each page gets its own VAO, so switching page while drawing is one bind
	glGenVertexArrays(1, &page.vao);
	glBindVertexArray(page.vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);//remembered by the VAO

	glBindBuffer(GL_ARRAY_BUFFER, page.buffer);
	//IPointer keeps them as integers in the shader (no conversion to float), locations match vertex_shader.glsl
	glVertexAttribIPointer(0, 3, GL_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, pos));
	glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, normal));
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void*)offsetof(PackedVertex, ao));
	glVertexAttribIPointer(3, 2, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, uv));
	glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, tile));
	glVertexAttribIPointer(placeAttribute, 1, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, place));
	for (int i = 0; i <= 4; i++) {
		glEnableVertexAttribArray(i);
	}
	glEnableVertexAttribArray(placeAttribute);

	// Chunk position, one per draw instead of one per vertex: divisor 1 means it advances once per instance,
	// and each draw's baseInstance says which entry it starts at, so draw i reads origins[i]
	glBindBuffer(GL_ARRAY_BUFFER, originBuffer);
	glVertexAttribPointer(originAttribute, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
	glVertexAttribDivisor(originAttribute, 1);
	glEnableVertexAttribArray(originAttribute);
	glBindVertexArray(0);

	int index = static_cast<int>(pages.size());
	pages.push_back(std::move(page));
	free(index, 0, vertices);
	if (index > 0) {
		std::cout << "Chunk mesh page " << index + 1 << " added (" << (gpuBytes() / (1024 * 1024)) << " MB in total)" << std::endl;
	}
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
	glBindBuffer(GL_COPY_WRITE_BUFFER, indexBuffer);//same buffer name, so every page VAO's binding stays valid
	glBufferData(GL_COPY_WRITE_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
}

size_t ChunkMeshPool::gpuBytes() const {
	size_t vertices = 0;
	for (const Page& page : pages) vertices += page.capacity;
	return vertices * sizeof(PackedVertex) + quadCapacity * 6 * sizeof(GLuint);
}

size_t ChunkMeshPool::usedBytes() const {
	return usedVertices * sizeof(PackedVertex) + quadCapacity * 6 * sizeof(GLuint);
}

// ---------- drawing ----------

void ChunkMeshPool::draw(const std::vector<DrawCommand>& commands, const std::vector<glm::vec3>& origins, const std::vector<int>& commandPages) const {
	if (commands.empty()) return;

	// One multi draw per page, so the commands are grouped by page (counting sort, keeps their order within a page)
	// baseInstance still points at each command's own origin, so the origins don't need reordering
	pageStarts.assign(pages.size() + 1, 0);
	for (int page : commandPages) pageStarts[page + 1]++;
	for (size_t p = 1; p < pageStarts.size(); p++) pageStarts[p] += pageStarts[p - 1];
	sortedCommands.resize(commands.size());
	std::vector<size_t> next(pageStarts.begin(), pageStarts.end() - 1);
	for (size_t i = 0; i < commands.size(); i++) sortedCommands[next[commandPages[i]]++] = commands[i];

	// New storage every call (orphaning), so the driver doesn't wait for the GPU to finish with last frame's lists
	glBindBuffer(GL_ARRAY_BUFFER, originBuffer);
	glBufferData(GL_ARRAY_BUFFER, origins.size() * sizeof(glm::vec3), origins.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer);
	glBufferData(GL_DRAW_INDIRECT_BUFFER, sortedCommands.size() * sizeof(DrawCommand), sortedCommands.data(), GL_STREAM_DRAW);

	for (size_t p = 0; p < pages.size(); p++) {
		GLsizei count = static_cast<GLsizei>(pageStarts[p + 1] - pageStarts[p]);
		if (count == 0) continue;
		glBindVertexArray(pages[p].vao);
		GLFunctions::multiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
			reinterpret_cast<const void*>(pageStarts[p] * sizeof(DrawCommand)), count, 0);
	}
	glBindVertexArray(0);
}
