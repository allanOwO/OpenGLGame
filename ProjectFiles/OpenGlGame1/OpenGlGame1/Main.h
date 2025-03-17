#pragma once

#include <GLFW/glfw3.h>
#include <fstream>//read files
#include <sstream>//put file into string
#include "Shader.h"
#include "stb/stb_image.h" 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class Main
{
public:
	Main();
	~Main();

	void run();

private:

	void init(); // Initialize GLFW, GLAD, etc.
	void processInput(GLFWwindow* window); // Process user input
	void createShaders();
	void createBufferObjects();
	void render();
	std::string loadShader(const char* filepath);

	GLFWwindow* window; // Window pointer

	unsigned int EBO, VBO, VAO, texture;//element buffer, vertex buffer, vertext array

	Shader* shader;

	// camera
	glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
};

