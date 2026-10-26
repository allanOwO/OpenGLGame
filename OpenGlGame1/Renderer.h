#pragma once
#include <memory>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Shader.h"
#include "Frustum.h"
#include "DebugStats.h"
#include "GpuTimer.h"
#include "ChunkMeshPool.h"

class World;
class Chunk;
struct ChunkRegion;
struct RaycastHit;

// Everything that talks to OpenGL for drawing a frame: shaders, textures, shadow map, sun and block outline
// Needs a current GL context when constructed and destroyed
class Renderer
{
public:
	Renderer();
	~Renderer();
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// highlight can be nullptr when the player isn't looking at a block
	// farPlane should reach past the furthest loaded chunk or the edge of the world gets cut off
	// zoom 1 is the normal 90 degree view, 4 makes everything look 4 times bigger
	void render(const World& world, const glm::vec3& cameraPos, const glm::mat4& view,
		int width, int height, float time, float farPlane, float zoom, const RaycastHit* highlight);

	// The camera's projection, also used by Main to tell World what a zoomed view would see
	static glm::mat4 projection(float zoom, float aspect, float farPlane);

	const RenderStats& getStats() const { return stats; }//counts from the last render()
	// GPU time of a recent render(), a few frames behind
	float getGpuMs() const { return shadowTimer.ms() + mainTimer.ms(); }
	float getGpuShadowMs() const { return shadowTimer.ms(); }//just the shadow map pass

private:
	void createShaders();
	void loadTextures();
	void createShadowMap();
	void createSun();
	void createHighlight();

	void renderShadowMap(const World& world);
	void drawChunks(const World& world, const glm::vec3& cameraPos);
	void drawHighlight(const RaycastHit& highlight, const glm::mat4& view, const glm::mat4& projection);
	void renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection, const glm::vec3& cameraPos);

	// shaders
	std::unique_ptr<Shader> shader;//chunks
	std::unique_ptr<Shader> sunShader;
	std::unique_ptr<Shader> depthShader;//shadow map pass
	std::unique_ptr<Shader> highlightShader;//plain coloured lines for the block outline

	// uniform locations, looked up once
	GLint viewLocation, projectionLocation, textureLocation, sunDirLoc, camPosLoc;
	GLint shadowMapLoc, mainLightSpaceLoc;
	GLint sunViewLoc, sunProjLoc, sunColourLoc, sunSunDirLoc, sunCamPosLoc;
	GLint lightSpaceLoc;
	GLint highlightModelLoc, highlightViewLoc, highlightProjLoc, highlightColourLoc;

	// buffers store data on gpu, vbo is vertex positions, ebo defines how these connect, vao acts like a container for these
	GLuint sunVAO = 0, sunVBO = 0, sunEBO = 0;
	GLuint highlightVAO = 0, highlightVBO = 0, highlightEBO = 0;
	GLuint texAtlas = 0;

	Frustum frustum;
	RenderStats stats;
	// Per frame draw lists, kept between frames so they don't reallocate
	struct VisibleSection {
		const MeshAllocation* mesh;
		glm::vec3 origin;
		glm::vec2 min, max;//its x/z extent in the world, to tell which face groups can point at the camera
	};
	std::vector<VisibleSection> visibleSections;
	std::vector<DrawCommand> drawCommands;
	std::vector<glm::vec3> drawOrigins;
	std::vector<int> drawPages;//which of the mesh pool's buffers each draw reads
	// Draws visibleSections, skipping face groups that point away from the viewer: from viewPoint (a camera),
	// or along viewDirection when there's no point (the sun is so far away it only has a direction)
	// Returns the triangles drawn
	int64_t drawList(const World& world, const glm::vec3* viewPoint, const glm::vec3& viewDirection);
	// Fills visibleSections with the sections inside the frustum that have a mesh, testing whole regions first
	// nearestFirst sorts regions by distance (chunks within a region are close together, so that's near enough for depth)
	// The main pass also adds far terrain (the shadow pass doesn't, shadows only cover the area around the player)
	void collectVisible(const World& world, const Frustum& view, const glm::vec3& cameraPos, bool nearestFirst, bool countStats);
	struct VisibleRegion {
		float distanceSquared;
		bool fullyInside;//its chunks can skip their own frustum tests
		const ChunkRegion* region;
	};
	std::vector<VisibleRegion> visibleRegions;

	// GPU time of each pass, measured separately so the overlay can show which one is expensive
	GpuTimer shadowTimer;
	GpuTimer mainTimer;//chunks, block outline and sun
	glm::vec3 sunDirection = glm::vec3(0.0f, -1.0f, -1.0f);//directional light

	// shadow stuff
	static const int SHADOW_WIDTH = 4096;
	static const int SHADOW_HEIGHT = 4096;
	static constexpr float SHADOW_RADIUS = 100.0f;//half width of the shadowed area around the player
	static constexpr float SHADOW_LIGHT_DISTANCE = 250.0f;//how far towards the sun the shadow camera sits
	GLuint depthMapFBO = 0;
	GLuint depthMap = 0;
	glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
};
