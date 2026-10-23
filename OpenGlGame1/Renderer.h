#pragma once
#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Shader.h"
#include "Frustum.h"

class World;
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
	void render(const World& world, const glm::vec3& cameraPos, const glm::mat4& view,
		int width, int height, float time, float farPlane, const RaycastHit* highlight);

private:
	void createShaders();
	void loadTextures();
	void createShadowMap();
	void createSun();
	void createHighlight();

	void renderShadowMap(const World& world);
	void drawChunks(const World& world);
	void drawHighlight(const RaycastHit& highlight, const glm::mat4& view, const glm::mat4& projection);
	void renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection, const glm::vec3& cameraPos);

	// shaders
	std::unique_ptr<Shader> shader;//chunks
	std::unique_ptr<Shader> sunShader;
	std::unique_ptr<Shader> depthShader;//shadow map pass
	std::unique_ptr<Shader> highlightShader;//plain coloured lines for the block outline

	// uniform locations, looked up once
	GLint modelLocation, viewLocation, projectionLocation, textureLocation, sunDirLoc, camPosLoc;
	GLint shadowMapLoc, mainLightSpaceLoc;
	GLint sunViewLoc, sunProjLoc, sunColourLoc, sunSunDirLoc, sunCamPosLoc;
	GLint lightSpaceLoc, depthModelLoc;
	GLint highlightModelLoc, highlightViewLoc, highlightProjLoc, highlightColourLoc;

	// buffers store data on gpu, vbo is vertex positions, ebo defines how these connect, vao acts like a container for these
	GLuint sunVAO = 0, sunVBO = 0, sunEBO = 0;
	GLuint highlightVAO = 0, highlightVBO = 0, highlightEBO = 0;
	GLuint texAtlas = 0;

	Frustum frustum;
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
