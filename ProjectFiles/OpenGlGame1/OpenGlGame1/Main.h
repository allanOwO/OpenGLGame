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

#include "Player.h"


class Main
{
public:
	Main();
	~Main();
	float width, height;

	void run();

private:

	void init(); // Initialize GLFW, GLAD, etc.
	void createShaders();
	void createCube();
	void createLight();
	void render();
	void getTextures();
	void doFps();
	void processInput(GLFWwindow* window);

	std::string loadShader(const char* filepath);
	unsigned int modelLocation, viewLocation, projectionLocation, textureLocation, lightColourLoc, lightPosLoc, sunDirLoc;
	unsigned int lightModelLocation, lightViewLocation, lightProjectionLocation;
	glm::vec3 mainLightPos = glm::vec3(0, 20, 10);
	glm::vec3 sunDirection = glm::vec3(0.0f, -1.0f, -1.0f);//directional light;

	GLFWwindow* window; // Window pointer

	std::unique_ptr<Player> player;  // Use smart pointer for automatic cleanup


	//buffers store data on gpu, vbo is vertext positions, ebo defines how these connect, vao acts like a container for these
	unsigned int VBO, normalsVBO, VAO, texture;// vertex buffer, vertext array
	unsigned int lightVBO, lightVAO;
	Shader* shader;
	Shader* lightShader;
	std::map<BlockType, GLuint> textureMap;

	//chunk stuff
	std::vector<Chunk> chunks;
	std::vector<glm::mat4> chunkModels;//array of chunk models
	void addChunks();
	void drawChunks();
	int seed = -1;

	//fps tracking & timing
	float lastFPSTime = 0.0f; // Time of the last FPS update
	int frameCount = 0;        // Number of frames since last update
	float fps = 0.0f;          // Current FPS value
	float deltaTime = 0.0f;	// Time between current frame and last frame
	float lastFrame = 0.0f; // Time of last frame


	//building
	void raycastBlock(); // Find block player is looking at
	void placeBlock(); // Place a block
	void breakBlock(); // Break a block
	float reachDistance = 5.0f; // Max distance player can build
	glm::vec3 highlightedBlockPos; // Position of the block to highlight
	bool hasHighlightedBlock;      // True if a block is in range and highlighted
	glm::vec3 prevBlock;
	void createHighlight();
	GLuint highlightVAO, highlightVBO;

	

};
	

