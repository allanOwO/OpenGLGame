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
#include <unordered_map>

#include "Player.h"
#include "Vec3Hash.h"
#include <future>//threading
#include <thread>
#include "MeshData.h"
#include <FastNoiseLite.h>
#include <glm/vec2.hpp>
#include "Frustum.h"

class Main
{
public:
	//thread count
	static const size_t MAX_ASYNC_TASKS() {
		return std::thread::hardware_concurrency();
	} 
		
	size_t activeAsyncTasks = 0; // Track running tasks
	std::recursive_mutex chunksMutex; 
	std::mutex logMutex;
	Main();
	~Main();
	float width, height;

	void run();
	Chunk* getChunk(const glm::vec3& pos);

	static FastNoiseLite noiseGen;
	static std::unordered_map<glm::ivec2, float, IVec2Hash> noiseCache; 
	std::mutex noiseMutex;



private:

	void init(); // Initialize GLFW, GLAD, etc.
	void createShaders();
	void createCube();
	void createSun();
	void renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection); 
	void render();
	void getTextures();
	void doFps();
	void processInput(GLFWwindow* window);

	std::string loadShader(const char* filepath);
	unsigned int modelLocation, viewLocation, projectionLocation, textureLocation, lightColourLoc, lightPosLoc, sunDirLoc, camPosLoc;
	unsigned int sunViewLoc, sunProjLoc ,sunColourLoc, sunSunDirLoc, sunCamPosLoc;
	glm::vec3 mainLightPos = glm::vec3(0, 20, 10);
	glm::vec3 sunDirection = glm::vec3(0.0f, -1.0f, -1.0f);//directional light;

	GLFWwindow* window; // Window pointer

	std::unique_ptr<Player> player;  // Use smart pointer for automatic cleanup
	Frustum frustum;


	//buffers store data on gpu, vbo is vertext positions, ebo defines how these connect, vao acts like a container for these
	unsigned int VBO, normalsVBO, VAO, texture;// vertex buffer, vertext array
	unsigned int sunVBO, sunVAO, sunEBO;
	Shader* shader;
	Shader* sunShader;
	std::map<BlockType, GLuint> textureMap;

	//chunk stuff
	//map to track async tasks for each chunk by its position. 
	std::unordered_map<glm::vec3, std::future<MeshData>, Vec3Hash> chunkMeshFutures;  
	std::unordered_map<glm::vec3, std::future<Chunk>, Vec3Hash> chunkGenerationFutures; // For async chunk generation 
	std::unordered_map<glm::vec3,Chunk,Vec3Hash> chunks;

	bool isInitialLoading; // Flag to track initial loading phase 
	int currentLoadingRadius; // Current radius for loading chunks 
	const int maxLoadingRadius; // Maximum radius (set to RENDER_DISTANCE) 



	std::vector<glm::mat4> chunkModels;//array of chunk models
	void updateChunks(const glm::vec3& playerPosition);
	void generateChunk(const glm::vec3& pos);
	void generateChunkAsync(const glm::vec3& pos);
	void tryApplyChunkGeneration();
	void drawChunks();
	int seed = -1;
	void updateChunkMeshAsync(Chunk& chunk);
	void tryApplyChunkMeshUpdate(Chunk& chunk);
	void processChunkMeshingInOrder();
	

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
	

