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

#include <filesystem>//to look for resourse file
#include <algorithm>


class Main
{
public:
	//thread count
	static size_t MAX_ASYNC_TASKS() {
		return std::max(1u, std::thread::hardware_concurrency());
	}

	Main();
	~Main();
	float width, height;

	void run();
	Chunk* getChunk(const glm::vec3& pos);//world position
	Chunk* findChunk(int chunkX, int chunkZ);//chunk coordinates
	BlockType getWorldBlock(const glm::ivec3& worldPos);//AIR if the chunk isn't loaded
	Player* getPlayer() const { return player.get(); }

	static FastNoiseLite noiseGen;//GetNoise is const, safe to share between generation threads
	//static std::unordered_map<glm::ivec2, float, IVec2Hash> noiseCache; //try without cache

	//only touched on the main thread, worker threads get copies (see ChunkMeshInput)
	std::unordered_map<uint64_t, Chunk> chunks;

	float getNoise(float x, float z) const;

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
	unsigned int modelLocation, viewLocation, projectionLocation, textureLocation, sunDirLoc, camPosLoc;
	GLint highlightModelLoc, highlightViewLoc, highlightProjLoc, highlightColourLoc;
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
	Shader* highlightShader;//plain coloured lines for the block outline
	GLuint texAtlas;

	//chunk stuff
	//async tasks for each chunk, keyed by chunk key
	std::unordered_map<uint64_t, std::future<MeshData>> chunkMeshFutures;
	std::unordered_map<uint64_t, std::future<Chunk>> chunkGenerationFutures; // For async chunk generation


	bool isInitialLoading; // Flag to track initial loading phase
	int currentLoadingRadius; // Current radius for loading chunks
	const int maxLoadingRadius; // Maximum radius (set to RENDER_DISTANCE)


	void updateChunks(const glm::vec3& playerPosition);
	void unloadFarChunks(const glm::ivec3& playerChunkPos);
	void generateChunkAsync(const glm::vec3& pos);
	void tryApplyChunkGeneration();
	void markNeighboursForRebuild(int chunkX, int chunkZ);
	void drawChunks();
	int seed = -1;
	void launchChunkMesh(uint64_t key, Chunk& chunk);
	void processChunkMeshing();
	void setWorldBlock(const glm::ivec3& worldPos, BlockType type);
	

	//fps tracking & timing
	float lastFPSTime = 0.0f; // Time of the last FPS update
	int frameCount = 0;        // Number of frames since last update
	float fps = 0.0f;          // Current FPS value
	float deltaTime = 0.0f;	// Time between current frame and last frame
	float lastFrame = 0.0f; // Time of last frame
	float physicsAccumulator = 0.0f; // Frame time not yet used by a physics step


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

	//shadow stuff
	void createShadowMap();
	void renderShadowMap();
	unsigned int depthMapFBO;
	unsigned int depthMap;
	Shader* depthShader;
	static const int SHADOW_WIDTH = 4096;
	static const int SHADOW_HEIGHT = 4096;
	static constexpr float SHADOW_RADIUS = 100.0f;//half width of the shadowed area around the player
	static constexpr float SHADOW_LIGHT_DISTANCE = 250.0f;//how far towards the sun the shadow camera sits
	glm::mat4 lightSpaceMatrix;
	GLint lightSpaceLoc, depthModelLoc;//depth shader
	GLint shadowMapLoc, mainLightSpaceLoc;//main shader

	//noise for world gen
	void initNoise();
	float getBiomeNoise(float x, float z) const;
	float remapHeight(float noiseValue, float biomeValue) const;
	float getWarpedHeight(float x, float z,float biomeValue) const;

	//to find atlas in both vs and build
	std::string getResourcePath(const std::string& subPath);

	//test
};
	

