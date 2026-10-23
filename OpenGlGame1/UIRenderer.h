#pragma once
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Shader.h"

// Draws 2D rectangles and pixel font text on top of the screen
// Everything added during a frame goes into one vertex buffer and is drawn with a single draw call in flush()
// Coordinates are in pixels with (0, 0) at the top left of the window
class UIRenderer
{
public:
	UIRenderer();
	~UIRenderer();
	UIRenderer(const UIRenderer&) = delete;
	UIRenderer& operator=(const UIRenderer&) = delete;

	void begin(int screenWidth, int screenHeight);
	void rect(float x, float y, float w, float h, const glm::vec4& colour);
	void text(const std::string& str, float x, float y, int scale, const glm::vec4& colour);
	void flush();//draws everything added since begin()

	static float textWidth(const std::string& str, int scale);
	static float textHeight(int scale);

private:
	struct Vertex {
		float x, y;
		float u, v;
		float r, g, b, a;
	};

	void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const glm::vec4& colour);

	std::unique_ptr<Shader> shader;
	GLint projectionLoc = -1;
	GLuint fontTexture = 0;
	GLuint VAO = 0, VBO = 0;

	std::vector<Vertex> vertices;
	int width = 1, height = 1;
};
