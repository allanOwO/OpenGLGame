#pragma once
#include <cstddef>
#include <map>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "VertexPacking.h"

// Where one chunk's mesh lives inside the pool
struct MeshAllocation {
	int page = 0;//which of the pool's vertex buffers
	GLint firstVertex = 0;//inside that page
	GLsizei vertexCount = 0;//used by the mesh
	GLsizei capacity = 0;//reserved, a bit more than vertexCount so small edits can rebuild in place
	GLsizei groupCounts[faceGroupCount] = {};//vertices of each FaceGroup, one after another from firstVertex

	GLsizei indexCount() const { return vertexCount / 4 * 6; }//every quad is 4 vertices, 6 indices
};

// Some vertices somewhere in the pool, not owned by anything (a slice of a mesh, for combine to copy)
struct MeshRange {
	int page;
	GLint first;
	GLsizei count;
};

// One draw in a glMultiDrawElementsIndirect call, layout fixed by OpenGL
struct DrawCommand {
	GLuint count;//indices
	GLuint instanceCount;//1, chunks aren't instanced
	GLuint firstIndex;//always 0, every chunk uses the start of the shared index buffer
	GLint baseVertex;//added to every index, this is what points the draw at the chunk's vertices
	GLuint baseInstance;//picks this draw's entry in the origin buffer (attribute 5 has divisor 1)
};

// Every chunk mesh in a few big vertex buffers, so all of them can be drawn with a handful of calls
//
// Before: one VAO + vertex buffer + index buffer per chunk, so drawing 6000 chunks was 6000 rounds of
// "set model matrix, bind VAO, draw", and that per draw overhead was most of the frame on both CPU and GPU
// Now: chunks get a range of a shared buffer (a simple free list allocator), and each frame the renderer
// writes one small DrawCommand per visible mesh and hands the list to the GPU in one call per buffer
//
// The buffers are fixed size pages, a new one is added when they're all full
// It used to be one buffer that doubled when full, but growing means making a bigger buffer and copying
// everything across, and the driver waits for the GPU to finish before it can: 70-120 ms freezes once
// it reached a few hundred MB (and at 4 GB the driver couldn't back it at all). Adding a page copies nothing
//
// Indices are shared too: every quad uses the same 0,1,2 0,2,3 pattern (see addQuad in Chunk.cpp),
// so one index buffer of 4q+{0,1,2,0,2,3} works for every chunk with baseVertex moving it to the chunk
//
// Main thread only, needs a GL 4.3 context
class ChunkMeshPool
{
public:
	ChunkMeshPool();
	~ChunkMeshPool();
	ChunkMeshPool(const ChunkMeshPool&) = delete;
	ChunkMeshPool& operator=(const ChunkMeshPool&) = delete;

	// Replace what's in allocation with these vertices (reuses its range when they fit)
	void upload(MeshAllocation& allocation, const std::vector<PackedVertex>& vertices, const int groupCounts[faceGroupCount]);
	void release(MeshAllocation& allocation);//give its range back, call before a chunk is destroyed
	// Replace target with all of parts one after another, copied on the GPU (nothing is sent from the CPU)
	// parts are left as they are, and may be inside target's old mesh (it's only released after copying)
	// parts must already be in face group order, groupCounts says how many of the vertices are in each group
	void combine(MeshAllocation& target, const std::vector<MeshRange>& parts, const GLsizei groupCounts[faceGroupCount]);

	// commands[i] is drawn with origins[i] as its chunk position, out of page pages[i]
	// const because it only streams per frame data to the GPU, the meshes themselves aren't changed
	void draw(const std::vector<DrawCommand>& commands, const std::vector<glm::vec3>& origins, const std::vector<int>& pages) const;

	size_t gpuBytes() const;//vertex + index buffers as allocated on the GPU, including unused space
	size_t usedBytes() const;//vertices meshes actually use + the shared index buffer
	int pageCount() const { return static_cast<int>(pages.size()); }

private:
	// One vertex buffer and the VAO that reads it (each VAO also has the shared index and origin buffers)
	struct Page {
		GLuint vao = 0;
		GLuint buffer = 0;
		GLsizei capacity = 0;//vertices
		std::map<GLint, GLsizei> freeRanges;//start vertex -> length, neighbours are merged so it stays short
	};

	MeshAllocation allocate(GLsizei vertices);//a free range in some page, adds a page if none has room
	void free(int page, GLint start, GLsizei vertices);
	void addPage(GLsizei vertices);
	void ensureIndices(size_t quads);

	std::vector<Page> pages;
	GLuint indexBuffer = 0;
	GLuint originBuffer = 0;//chunk position per draw, attribute 5
	GLuint commandBuffer = 0;//DrawCommands, GL_DRAW_INDIRECT_BUFFER

	size_t quadCapacity = 0;//quads the shared index buffer covers
	size_t usedVertices = 0;

	// draw() sorts commands by page into these, kept so they don't reallocate every frame
	mutable std::vector<DrawCommand> sortedCommands;
	mutable std::vector<size_t> pageStarts;
};
