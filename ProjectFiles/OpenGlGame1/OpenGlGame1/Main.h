#pragma once

#include <GLFW/glfw3.h>
#include <fstream>//read files
#include <sstream>//put file into string
#include "Shader.h"
#include "stb/stb_image.h" 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Chunk.h"
#include <map>

class Main
{
public:
	Main();
	~Main();
	float width, height;

	void run();

private:

	void init(); // Initialize GLFW, GLAD, etc.
	void processInput(GLFWwindow* window); // Process user input
	void createShaders();
	void createCube();
	void createLight();
	void render();
	void getTextures();
	void doFps();

	void mouse_callback(GLFWwindow* window, double xpos, double ypos);

	std::string loadShader(const char* filepath);
	unsigned int modelLocation, viewLocation, projectionLocation, textureLocation,lightLocation;
	unsigned int lightModelLocation, lightViewLocation, lightProjectionLocation;

	int seed = -1;
	void drawChunks();

	GLFWwindow* window; // Window pointer
	

	//buffers store data on gpu, vbo is vertext positions, ebo defines how these connect, vao acts like a container for these
	unsigned int VBO, VAO, texture;//element buffer, vertex buffer, vertext array

	unsigned int lightVBO, lightVAO;

	Shader* shader;
	Shader* lightShader;

	std::map<BlockType, GLuint> textureMap; 

	// camera
	bool firstMouse;//used to ignore the mouses 1st frame to stop a large jump
	glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

	glm::vec3 lookDirection;
	float yaw = -90.0f;//offest at beginign to allign to -z
	float pitch = 0.0f;

	const float camSpeedBase = 10.0f;

	float deltaTime = 0.0f;	// Time between current frame and last frame
	float lastFrame = 0.0f; // Time of last frame

	//mouse movement
	float lastX = 400, lastY = 300;

	//chunk stuff
	std::vector<Chunk> chunks;
	std::vector<glm::mat4> chunkModels;//array of chunk models
	void addChunks();

	//fps tracking
	float lastFPSTime = 0.0f; // Time of the last FPS update
	int frameCount = 0;        // Number of frames since last update
	float fps = 0.0f;          // Current FPS value
};

