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

	void mouse_callback(GLFWwindow* window, double xpos, double ypos);

	std::string loadShader(const char* filepath);

	GLFWwindow* window; // Window pointer

	unsigned int EBO, VBO, VAO, texture;//element buffer, vertex buffer, vertext array

	Shader* shader;

	// camera
	glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

	glm::vec3 lookDirection;
	float yaw = -90.0f;//offest at beginign to allign to -z
	float pitch = 0.0f;

	const float camSpeedBase = 2.0f;

	float deltaTime = 0.0f;	// Time between current frame and last frame
	float lastFrame = 0.0f; // Time of last frame
};

