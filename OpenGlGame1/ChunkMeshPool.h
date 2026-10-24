#pragma once
#include <cstddef>
#include <map>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "VertexPacking.h"

// Where one chunk's mesh lives inside the pool's vertex buffer
struct MeshAllocation {
	GLint firstVertex = 0;
	GLsizei vertexCount = 0;//used by the mesh
	GLsizei capacity = 0;//reserved, a bit more than vertexCount so small edits can rebuild in place

	GLsizei indexCount() const { return vertexCount / 4 * 6; }//every quad is 4 vertices, 6 indices
};

// One draw in a glMultiDrawElementsIndirect call, layout fixed by OpenGL
struct DrawCommand {
	GLuint count;//indices
	GLuint instanceCount;//1, chunks aren't instanced
	GLuint firstIndex;//always 0, every chunk uses the start of the shared index buffer
	GLint baseVertex;//added to every index, this is what points the draw at the chunk's vertices
	GLuint baseInstance;//picks this draw's entry in the origin buffer (attribute 5 has divisor 1)
};

// Every chunk mesh in one vertex buffer, so all of them can be drawn with one call
//
// Before: one VAO + vertex buffer + index buffer per chunk, so drawing 6000 chunks was 6000 rounds of
// "set model matrix, bind VAO, draw", and that per draw overhead was most of the frame on both CPU and GPU
// Now: chunks get a range of the shared buffer (a simple free list allocator), and each frame the renderer
// writes one small DrawCommand per visible chunk and hands the whole list to the GPU in one call
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
	void upload(MeshAllocation& allocation, const std::vector<PackedVertex>& vertices);
	void release(MeshAllocation& allocation);//give its range back, call before a chunk is destroyed

	// commands[i] is drawn with origins[i] as its chunk position, the VAO must be bound by the caller (bindForDrawing)
	// const because it only streams per frame data to the GPU, the meshes themselves aren't changed
	void bindForDrawing() const;
	void draw(const std::vector<DrawCommand>& commands, const std::vector<glm::vec3>& origins) const;

	size_t gpuBytes() const;//vertex + index buffers as allocated on the GPU, including unused space
	size_t usedBytes() const;//vertices meshes actually use + the shared index buffer

private:
	GLint allocate(GLsizei vertices);//start of a free range, grows the buffer if nothing fits
	void free(GLint start, GLsizei vertices);
	void growVertexBuffer(size_t minimumVertices);
	void ensureIndices(size_t quads);
	void setVertexAttributes();//points the VAO at vertexBuffer, again after it's replaced by a bigger one

	GLuint vao = 0;
	GLuint vertexBuffer = 0;
	GLuint indexBuffer = 0;
	GLuint originBuffer = 0;//chunk position per draw, attribute 5
	GLuint commandBuffer = 0;//DrawCommands, GL_DRAW_INDIRECT_BUFFER

	size_t vertexCapacity = 0;
	size_t quadCapacity = 0;//quads the shared index buffer covers
	size_t usedVertices = 0;

	std::map<GLint, GLsizei> freeRanges;//start vertex -> length, neighbours are merged so it stays short
};
