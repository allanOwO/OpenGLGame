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

#include "Mob.h"
#include "Bee.h"


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

	std::unordered_map<uint64_t, Chunk> chunks;

	float getNoise(float x, float z);

private:

	void init(); // Initialize GLFW, GLAD, etc.
	void createShaders();
	void createSun();
	void renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection); 
	void render();
	void getTextures();
	void doFps();
	void processInput(GLFWwindow* window);

	std::string loadShader(const char* filepath);
	unsigned int 
		modelLocation, viewLocation, projectionLocation, textureLocation,
		lightColourLoc, lightPosLoc,
		sunDirLoc, camPosLoc,
		entityModelLoc,entityViewLoc,entityProjLoc;

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
	Shader* entityShader;
	GLuint texAtlas;

	//chunk stuff
	//map to track async tasks for each chunk by its position. 
	std::unordered_map<glm::vec3, std::future<MeshData>, Vec3Hash> chunkMeshFutures;  
	std::unordered_map<uint64_t, std::future<void>> chunkGenerationFutures; // For async chunk generation 
	

	bool isInitialLoading; // Flag to track initial loading phase 
	int currentLoadingRadius; // Current radius for loading chunks 
	const int maxLoadingRadius; // Maximum radius (set to RENDER_DISTANCE) 



	std::vector<glm::mat4> chunkModels;//array of chunk models
	void updateChunks(const glm::vec3& playerPosition);
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
	float fps = 0.0f;         
	float deltaTime = 0.0f;
	float lastFrame = 0.0f; //Time of last frame


	//building
	void raycastBlock(); //block player is looking at
	void placeBlock(); 
	void breakBlock(); 
	float reachDistance = 5.0f;
	glm::vec3 highlightedBlockPos;
	bool hasHighlightedBlock;      //true if a block is in range and highlighted
	glm::vec3 prevBlock;
	void createHighlight();
	GLuint highlightVAO, highlightVBO;

	//shadow stuff
	void createShadowMap();
	void renderShadowMap();
	unsigned int depthMapFBO;
	unsigned int depthMap;
	Shader* depthShader;
	static const int SHADOW_WIDTH = 4096*2;
	static const int SHADOW_HEIGHT = 4096*2;
	glm::mat4 lightSpaceMatrix;
	unsigned int lightSpaceLoc, shadowMapLoc;

	//noise for world gen
	void initNoise();
	inline float getBiomeNoise(float x, float z);
	inline float remapHeight(float noiseValue, float biomeValue);

	inline float getWarpedHeight(float x, float z,float biomeValue);

	//list of mobs
	std::vector<std::unique_ptr<Mob>> entities;
};
	

