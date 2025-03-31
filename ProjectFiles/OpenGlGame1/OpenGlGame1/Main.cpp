#include <iostream>
#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include "Main.h"
#define STB_IMAGE_IMPLEMENTATION  // This tells stb to include the implementation
#include "stb/stb_image.h"          // Path to the stb_image.h file

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <random>
#include <memory>
#include <unordered_set>

#define CHUNK_SIZE 16
#define RENDER_DISTANCE 4
#define SUN_TILT glm::radians(70.0f)
#define SUN_SPEED 0.3f

FastNoiseLite Main::noiseGen;
std::unordered_map<glm::ivec2, float, IVec2Hash> Main::noiseCache; 


Main::Main() : window(nullptr),width(1280),height(720),player(nullptr)
                , isInitialLoading(true), currentLoadingRadius(0), maxLoadingRadius(RENDER_DISTANCE) {

    init();
    player = std::make_unique<Player>(window,this);  // Use smart pointer for automatic cleanup;//give window to player 
}

Main::~Main() {
    player.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    delete depthShader;
}

std::string Main::loadShader(const char* filepath) {
    std::ifstream file(filepath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    //update viewport
    glViewport(0, 0, width, height);

    // Update the width and height in the Main class
    Main* mainInstance = static_cast<Main*>(glfwGetWindowUserPointer(window));
    if (mainInstance) {
        mainInstance->width = width;
        mainInstance->height = height; 
    }
}

void Main::init() {

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        exit(-1);
    }

    // Set OpenGL version to 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); 
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 


    // Create a window
    window = glfwCreateWindow(width, height, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        exit(-1);
    }


    // Make the OpenGL context current
    glfwMakeContextCurrent(window);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD!" << std::endl;
        exit(-1);
    }

    // Set framebuffer size callback  
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    createShadowMap();

    //some opengl settings
    glEnable(GL_CULL_FACE);   // Enable face culling 
    glCullFace(GL_BACK);      // Cull back faces (only render front faces) 
    glFrontFace(GL_CCW);      // Define front faces as counterclockwise (CCW) 
    glEnable(GL_DEPTH_TEST); 
    glfwSwapInterval(0);// 1 = V-Sync on, 0 = V-Sync off 
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);//wireframe mode 

    
}

void Main::initNoise() {

    if (seed == -1) {
        std::random_device rd;
        seed = rd();
        std::cout << seed;
    }

    //create noise for world gen
    noiseGen.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noiseGen.SetFrequency(0.03f); // Lower frequency for smoother terrain
    noiseGen.SetSeed(seed);

    int range = CHUNK_SIZE * RENDER_DISTANCE;

    for (int x = -range; x < range; x++) {
        for (int z = -range; z < range; z++) {

            noiseCache[{x, z}] = getNoise(static_cast<float>(x), static_cast<float>(z));  // Fixed value 
        }
    }
}

float Main::getNoise(float x, float z) {

    glm::ivec2 noiseKey(x, z);

    //if noise cached
    if (noiseCache.find(noiseKey) != noiseCache.end()) {
        return noiseCache[noiseKey];
    }
    else
    {
        noiseCache[noiseKey] = remapHeight(getWarpedHeight(x, z));
        return(noiseCache[noiseKey]);
    }

}

inline float Main::remapHeight(float noiseValue) {
    // Input noiseValue is now 0 to 1
    float flatness = 0.5f;  // Portion of terrain kept flat
    float steepness = 2.0f; // Steepness of mountains
    float remapped;

   
    remapped = flatness + (1.0f - flatness) * pow((noiseValue - flatness) / (1.0f - flatness), steepness);
    

    // Define world bounds
    float minHeight = 32.0f; // Minimum terrain height (above baseTerrainHeight)
    float maxHeight = 150.0f; // Maximum terrain height
    return minHeight + (remapped* (maxHeight) ); // Scale to minHeight to maxHeight
}

inline float Main::getWarpedHeight(float x, float z) {

    float warpScale = 0.2f;
    float warpX = Main::noiseGen.GetNoise(x * warpScale + 10.0f, z * warpScale + 10.0f) * 10.0f;
    float warpZ = Main::noiseGen.GetNoise(x * warpScale + 20.0f, z * warpScale + 20.0f) * 10.0f;
    float warpedX = x + warpX;
    float warpedZ = z + warpZ;

    float height = 0.0f;
    float amplitude = 0.5f;
    float frequency = 0.3f;
    float lacunarity = 2.0f;
    float persistence = 0.5f;
    int octaves = 3;
    float totalAmplitude = 0.0f; // To normalize the range

    for (int i = 0; i < octaves; i++) {
        float sample = Main::noiseGen.GetNoise(warpedX * frequency, warpedZ * frequency); // -1 to 1
        height += sample * amplitude;
        totalAmplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    // Normalize to 0-1
    height = (height + totalAmplitude) / (2.0f * totalAmplitude); // Shift from [-A, A] to [0, 2A], then to [0, 1]
    return height;

}

void Main::createShadowMap() {
    // Generate and configure depth texture
    glGenTextures(1, &depthMap); 
    glBindTexture(GL_TEXTURE_2D, depthMap); 
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor); 

    // Generate and configure framebuffer
    glGenFramebuffers(1, &depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE); // No color buffer for shadow map
    glReadBuffer(GL_NONE);
    


    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow map framebuffer incomplete: " << status << std::endl;
        switch (status) {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            std::cerr << "Incomplete attachment" << std::endl; break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            std::cerr << "Missing attachment" << std::endl; break;
        default:
            std::cerr << "Other error" << std::endl; break;
        }
        exit(-1); // Or handle gracefully
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0); 
}

void Main::processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)//check for esc pressed
        glfwSetWindowShouldClose(window, true);

    //buiilding
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        static double lastBreakTime = 0.0;
        double currentTime = glfwGetTime();
        if (currentTime - lastBreakTime > 0.2) { // 0.2s cooldown
            breakBlock();
            lastBreakTime = currentTime;
        }
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        static double lastPlaceTime = 0.0;
        double currentTime = glfwGetTime();
        if (currentTime - lastPlaceTime > 0.2) { // 0.2s cooldown
            placeBlock();
            lastPlaceTime = currentTime;
        }
    }
}

void Main::createShaders() {
    shader = new Shader("vertex_shader.glsl", "fragment_shader.glsl");
    sunShader = new Shader("sunVertex.glsl","sunFragment.glsl");
    depthShader = new Shader("depth_vertex.glsl", "depth_fragment.glsl");

    if (sunShader->ID == 0) {
        std::cerr << "Sun shader failed to compile/link" << std::endl;
    }
    std::cout << "Sun shader ID: " << sunShader->ID << std::endl; // Verify ID 

    shader->use();  
    modelLocation = glGetUniformLocation(shader->ID, "model");
    viewLocation = glGetUniformLocation(shader->ID, "view");
    projectionLocation = glGetUniformLocation(shader->ID, "projection");
    textureLocation = glGetUniformLocation(shader->ID, "ourTexture");
    lightColourLoc = glGetUniformLocation(shader->ID, "lightColour");
    lightPosLoc = glGetUniformLocation(shader->ID, "lightPos"); // New
    sunDirLoc = glGetUniformLocation(shader->ID, "sunDirection");
    camPosLoc = glGetUniformLocation(shader->ID, "cameraPos");

    sunShader->use();

    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram != sunShader->ID) {
        std::cerr << "Sun shader not active after use(). Expected: " << sunShader->ID << ", Got: " << currentProgram << std::endl;
    }

    sunViewLoc = glGetUniformLocation(sunShader->ID, "view");
    sunProjLoc = glGetUniformLocation(sunShader->ID, "projection");
    sunSunDirLoc = glGetUniformLocation(sunShader->ID, "sunDirection");
    sunColourLoc = glGetUniformLocation(sunShader->ID, "sunColour");
    sunCamPosLoc = glGetUniformLocation(sunShader->ID, "camPos");

    depthShader->use();
    lightSpaceLoc = glGetUniformLocation(depthShader->ID, "lightSpaceMatrix");
    shadowMapLoc = glGetUniformLocation(depthShader->ID, "shadowMap");

    if (lightSpaceLoc == -1) {
        std::cerr << "Uniform 'lightSpaceMatrix' not found in depth shader" << std::endl;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram != depthShader->ID) {
        std::cerr << "deoth shader not active after use(). Expected: " << depthShader->ID << ", Got: " << currentProgram << std::endl;
    }

}

void Main::createHighlight() {
    float vertices[] = {
        -0.51f, -0.51f, -0.51f,  0.51f, -0.51f, -0.51f,
         0.51f,  0.51f, -0.51f, -0.51f,  0.51f, -0.51f,
        -0.51f, -0.51f,  0.51f,  0.51f, -0.51f,  0.51f,
         0.51f,  0.51f,  0.51f, -0.51f,  0.51f,  0.51f
    };
    unsigned int indices[] = {
        0, 1, 1, 2, 2, 3, 3, 0, // Bottom face
        4, 5, 5, 6, 6, 7, 7, 4, // Top face
        0, 4, 1, 5, 2, 6, 3, 7  // Sides
    };

    glGenVertexArrays(1, &highlightVAO);
    glGenBuffers(1, &highlightVBO);
    GLuint EBO;
    glGenBuffers(1, &EBO);

    glBindVertexArray(highlightVAO);
    glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Main::createSun() {
    // Vertex data for sun quad
    float sunVertices[] = {
        -0.5f,  0.5f,  0.0f, 1.0f, // Top-left
         0.5f,  0.5f,  1.0f, 1.0f, // Top-right
         0.5f, -0.5f,  1.0f, 0.0f, // Bottom-right
        -0.5f, -0.5f,  0.0f, 0.0f  // Bottom-left
    };
    unsigned int sunIndices[] = { 0, 3, 2, 2, 1, 0 };//reversed winding order

    glGenVertexArrays(1, &sunVAO);
    glGenBuffers(1, &sunVBO);
    glGenBuffers(1, &sunEBO);

    glBindVertexArray(sunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Main::renderShadowMap() {
    // Render depth map
    while (glGetError() != GL_NO_ERROR) {}

    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    GLenum err = glGetError(); 
    if (err != GL_NO_ERROR) std::cerr << "Error after glViewport: " << err << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    err = glGetError();
    if (err != GL_NO_ERROR) std::cerr << "Error after glBindFramebuffer: " << err << std::endl;

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer error: " << status << std::endl;
        return;
    }

    glDepthMask(GL_TRUE); // Ensure depth writes are enabled
    glClear(GL_DEPTH_BUFFER_BIT);
    err = glGetError();
    if (err != GL_NO_ERROR) std::cerr << "Error after glClear: " << err << std::endl;
    depthShader->use();

    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram != depthShader->ID) {
        std::cerr << "depth shader not active after use(). Expected: " << depthShader->ID << ", Got: " << currentProgram << std::endl;
    }

    

    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
     err = glGetError();
    if (err != GL_NO_ERROR) std::cerr << "Error after unifrm light mat loc: " << err << std::endl;

    //render chunks 1st pass to create shadows
    std::lock_guard<std::recursive_mutex> lock(chunksMutex);
    for (const auto& pair : chunks) {
        const Chunk& chunk = pair.second;
        if (!chunk.isActive) continue;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), chunk.chunkPosition);
        glUniformMatrix4fv(glGetUniformLocation(depthShader->ID, "model"), 1, GL_FALSE, glm::value_ptr(model));
         err = glGetError();
        if (err != GL_NO_ERROR) std::cerr << "Error after model loc: " << err << std::endl;
        glBindVertexArray(chunk.VAO);
         err = glGetError();
        if (err != GL_NO_ERROR) std::cerr << "Error after bind vao shadow: " << err << std::endl;
        for (const auto& typePair : chunk.indicesByType) {
            const auto& indices = typePair.second;
            if (indices.empty()) continue;
            glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT,
                (void*)(chunk.baseIndicesByType.at(typePair.first) * sizeof(unsigned int)));
        }
        glBindVertexArray(0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //clear screen for main pass
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Main::drawChunks() { 
    shader->use();

    std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Separate lock for rendering
    for (const auto& pair : chunks) {
        
        const Chunk& chunk = pair.second;
        const glm::vec3& pos = chunk.chunkPosition;

        // Frustum culling 
        glm::vec3 min = pos;
        glm::vec3 max = pos + glm::vec3(CHUNK_SIZE, chunk.currentTallestBlock+1, CHUNK_SIZE);
        if (!frustum.isBoxInFrustum(min, max)) {
            continue; // Skip chunks outside the frustum 
        }
        if (!chunk.isActive)
            continue;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));

        glBindVertexArray(chunk.VAO);
        for (const auto& typePair : chunk.verticesByType) {
            BlockType type = typePair.first;
            const auto& indices = chunk.indicesByType.at(type);

            if (indices.empty()) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texAtlas);
            glUniform1i(textureLocation, 0);

            glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT,
                (void*)(chunk.baseIndicesByType.at(type) * sizeof(unsigned int)));
        }
        glBindVertexArray(0);
    }
}

void Main::renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection) {

    float sunAngle = glm::dot(sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));
    float transitionFactor = (-sunAngle + 1.0f) * 0.5f;
    glm::vec3 sunColour = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.0f), transitionFactor);

    sunShader->use();


    glUniformMatrix4fv(sunViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(sunProjLoc, 1, GL_FALSE, glm::value_ptr(projection));  
    glUniform3fv(sunSunDirLoc, 1, glm::value_ptr(sunDirection));
    glUniform3fv(sunColourLoc, 1, glm::value_ptr(sunColour));
    glUniform3fv(sunCamPosLoc, 1, glm::value_ptr(player->getCameraPos()));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glDisable(GL_DEPTH_TEST); 

    glBindVertexArray(sunVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0); 
    glEnable(GL_DEPTH_TEST); 
    glDisable(GL_BLEND);  
}

void Main::render() {

    // set background & clear screen
    glClearColor(0.1f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

    //vie and projection matrices
    glm::mat4 view = player->getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)width / (float)height, 0.1f, 1000.0f);
     
    //whats in current view
    glm::mat4 viewProj = projection * view; 
    frustum.update(viewProj); // Update frustum for culling 

    // Rotate sun direction based on time
    float time = glfwGetTime();
    float angle = time * SUN_SPEED;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle) * sin(SUN_TILT), sin(angle) * cos(SUN_TILT))); // Compute sun direction using a circular motion
    sunDirection = -sunDir; 

    //shadow code, smaller area gives better shadows with same resolution
    glm::vec3 playerPos = player->getCameraPos();  
    glm::mat4 lightProjection = glm::ortho(-100.0f + playerPos.x, 100.0f + playerPos.x,
        -100.0f + playerPos.z, 100.0f + playerPos.z, 
        1.0f, 200.0f);

    glm::vec3 lightPos = playerPos - normalize (sunDirection) * 50.0f; // Position sun far along its direction
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0, 1.0, 0.0));
    lightSpaceMatrix = lightProjection * lightView;

    renderShadowMap();

    // Ensure default framebuffer and clear errors
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    //use normal shader for cube + chunks
    shader->use(); 

    // pass view and projection matrices to shader
    glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(camPosLoc, 1, glm::value_ptr(player->getCameraPos()));
    glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDirection));
    //glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
    
    

    GLint shadowMapLoc = glGetUniformLocation(shader->ID, "shadowMap"); 
    glUniform1i(shadowMapLoc, 1); 

    GLint lightSpaceLoc = glGetUniformLocation(shader->ID, "lightSpaceMatrix");
    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix)); 

    // Bind shadow map to texture unit 1 (unit 0 is for texture atlas)
    glActiveTexture(GL_TEXTURE1); 
    glBindTexture(GL_TEXTURE_2D, depthMap); 

    // Bind texture atlas to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    glUniform1i(textureLocation, 0); 

    drawChunks();

    raycastBlock();//used to find currently faced blocl
    // Render highlight if a block is in range
    if (hasHighlightedBlock) {
        shader->use();
        glm::mat4 highlightModel = glm::mat4(1.0f);
        // Center the highlight by adding (0.5, 0.5, 0.5) to the block position
        highlightModel = glm::translate(highlightModel, highlightedBlockPos + glm::vec3(0.5f, 0.5f, 0.5f));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(highlightModel));
        glUniform4f(lightColourLoc, 1.0f, 1.0f, 0.0f, 1.0f); // Yellow highlight

        glBindVertexArray(highlightVAO);
        glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0); // 24 indices for wireframe
        glBindVertexArray(0);
    }

    renderSun(view,projection,-sunDirection); 
}

void Main::generateChunkAsync(const glm::vec3& pos) {
    // Launch async task to generate the chunk

    int chunkX = static_cast<int>(pos.x / CHUNK_SIZE);
    int chunkZ = static_cast<int>(pos.z / CHUNK_SIZE);
    uint64_t key = getChunkKey(chunkX, chunkZ);

    chunkGenerationFutures[key] = std::async(std::launch::async, [this, pos,key]() {
        Chunk chunk(pos, seed, this);
        std::lock_guard<std::recursive_mutex> lock(chunksMutex); 
        chunks.emplace(key, std::move(chunk)); // Store in chunks with the uint64_t key
        }); 
}

void Main::tryApplyChunkGeneration() {
    std::lock_guard<std::recursive_mutex> lock(chunksMutex);
    GLFWwindow* currentContext = glfwGetCurrentContext(); 
    if (currentContext != window) {
        std::cerr << "Context not current before chunk init! Current: " << currentContext << ", Expected: " << window << std::endl;
        glfwMakeContextCurrent(window);
    }
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "Pending OpenGL error before chunk init: " << err << std::endl;
    } 
    for (auto it = chunkGenerationFutures.begin(); it != chunkGenerationFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            
            uint64_t key = it->first; // The key is the uint64_t chunk identifier
            auto chunkIt = chunks.find(key);
            if (chunkIt != chunks.end()) {
                Chunk& chunk = chunkIt->second;

                // Initialize buffers for rendering
                chunk.initializeBuffers(); 

                chunkModels.emplace_back(glm::translate(glm::mat4(1.0f), chunk.chunkPosition));
            }

            // Remove the completed future
            it = chunkGenerationFutures.erase(it); 
            
        }
        else {
            ++it;
        }
    }
}

void Main::updateChunks(const glm::vec3& playerPosition) {
    // Calculate the player's chunk position in chunk coordinates
    glm::ivec3 playerChunkPos(
        static_cast<int>(floor(playerPosition.x / CHUNK_SIZE)), 
        0, // Y is fixed at 0 for chunk coordinates
        static_cast<int>(floor(playerPosition.z / CHUNK_SIZE))
    );
    uint64_t playerKey = getChunkKey(playerChunkPos.x, playerChunkPos.z); 

    // Store chunk positions and distances
    std::vector<std::pair<uint64_t, float>> chunkPositions;
    int effectiveRenderDistance = isInitialLoading ? currentLoadingRadius : RENDER_DISTANCE;
    chunkPositions.reserve((2 * effectiveRenderDistance + 1) * (2 * effectiveRenderDistance + 1)); // Preallocate 

    for (int x = -effectiveRenderDistance; x <= effectiveRenderDistance; x++) {
        for (int z = -effectiveRenderDistance; z <= effectiveRenderDistance; z++) {
            glm::ivec3 chunkPos = playerChunkPos + glm::ivec3(x, 0, z); // Offset in chunk coordinates
            uint64_t key = getChunkKey(chunkPos.x, chunkPos.z);  
            glm::vec3 realChunkPos = glm::vec3(chunkPos.x * CHUNK_SIZE, -Chunk::baseTerrainHeight, chunkPos.z * CHUNK_SIZE);
            
            // Use Manhattan distance for faster sorting (no square root needed)
            float distance = std::abs(realChunkPos.x - playerPosition.x) + std::abs(realChunkPos.z - playerPosition.z);
            chunkPositions.emplace_back(key, distance);
        }
    }

    // Sort by distance
    std::sort(chunkPositions.begin(), chunkPositions.end(),
        [](const std::pair<uint64_t, float>& a, const std::pair<uint64_t, float>& b) {
            return a.second < b.second;
        });

    std::unordered_set<uint64_t> loadedChunks;
    loadedChunks.reserve(chunkPositions.size());//reduce rehashing
    int chunksGeneratedThisFrame = 0;
    const int maxChunksGeneratedPerFrame = isInitialLoading ? 2 : 1; // Load 2 chunks per frame during initial loading
    
    {
        std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Lock here
        for (const auto& pair : chunkPositions) {
            if (chunksGeneratedThisFrame >= maxChunksGeneratedPerFrame) break;

            loadedChunks.insert(pair.first);
            int chunkX = static_cast<int32_t>(pair.first >> 32); // Extract x
            int chunkZ = static_cast<int32_t>(pair.first & 0xFFFFFFFF); // Extract z
            glm::vec3 realChunkPos(chunkX * CHUNK_SIZE, -Chunk::baseTerrainHeight, chunkZ * CHUNK_SIZE);

            if (chunks.find(pair.first) == chunks.end() && chunkGenerationFutures.find(pair.first) == chunkGenerationFutures.end()) {
                generateChunkAsync(realChunkPos);
                chunksGeneratedThisFrame++;
            }
        } 
    }
    

    // Update the loading radius
    if (isInitialLoading) {
        std::lock_guard<std::recursive_mutex> lock(chunksMutex);
        // Check if all chunks within the current radius are loaded
        int expectedChunks = (2 * currentLoadingRadius + 1) * (2 * currentLoadingRadius + 1);
        int loadedChunksCount = 0;
        for (const auto& pair : chunks) {
            
            int dx = std::abs(pair.second.chunkPosition.x / CHUNK_SIZE - playerChunkPos.x);
            int dz = std::abs(pair.second.chunkPosition.z / CHUNK_SIZE - playerChunkPos.z);
            if (dx <= currentLoadingRadius && dz <= currentLoadingRadius) {
                loadedChunksCount++;
            }
        }
        if (loadedChunksCount >= expectedChunks) {
            currentLoadingRadius++;
            if (currentLoadingRadius > maxLoadingRadius) {
                isInitialLoading = false;
                std::cout << "Initial loading complete! All chunks within render distance loaded." << std::endl;
            }
        }
    }

    // Mark chunks as active/inactive
    std::lock_guard<std::recursive_mutex> lock(chunksMutex);
    for (auto& pair : chunks) {
        if (loadedChunks.find(pair.first) == loadedChunks.end()) {
            pair.second.isActive = false;
        }
        else {
            pair.second.isActive = true;
        }
    }
}

Chunk* Main::getChunk(const glm::vec3& pos) {
    std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Keep this for external access

    uint64_t key = getChunkKey(pos.x, pos.z);
    // Look up the chunk
    auto it = chunks.find(key);
    return (it != chunks.end()) ? &it->second : nullptr;
} 



void Main::getTextures() {

    int width, height, nrChannels;

    // Load another texture for a different block type, e.g., "stone.jpg"
    unsigned char* data = stbi_load("../ResourceFiles/textureAtlas.png", &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load atlas" << std::endl;
        exit(-1);
    }

    // Determine texture format based on loaded image
    GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
   
    glGenTextures(1, &texAtlas);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    //wrap and fill params
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);

}

void Main::doFps() {
    float currentTime = glfwGetTime();
    frameCount++;

    // Update FPS every second
    if (currentTime - lastFPSTime >= 1.0f) {
        fps = frameCount / (currentTime - lastFPSTime);
        frameCount = 0;
        lastFPSTime = currentTime;

        // Update window title with FPS
        std::string title = "My Game - FPS: " + std::to_string(fps);
        glfwSetWindowTitle(window, title.c_str());
    }
}
 

void Main::raycastBlock() {
    hasHighlightedBlock = false; 
    glm::vec3 rayOrigin = player->getCameraPos();
    glm::vec3 rayDir = glm::normalize(player->getCameraFront());
    float t = 0.0f;
    float step = 0.1f;

    for (t = 0.0f; t < reachDistance; t += step) {
        glm::vec3 currentPos = rayOrigin + rayDir * t;

        // Calculate the chunk position (assuming chunks are keyed at multiples of chunkSize in X and Z)
        glm::vec3 chunkPos = glm::floor(currentPos / glm::vec3(Chunk::chunkSize, 1, Chunk::chunkSize)) * glm::vec3(Chunk::chunkSize, 0, Chunk::chunkSize);
        chunkPos.y = -Chunk::baseTerrainHeight; // Set to the fixed Y-position of chunks (adjust as needed)

        // Look up the chunk in the map
        uint64_t key = getChunkKey(chunkPos.x / CHUNK_SIZE, chunkPos.z / CHUNK_SIZE); 
        Chunk* chunk = nullptr;
        { 
            std::lock_guard<std::recursive_mutex> lock(chunksMutex);
            auto it = chunks.find(key);
            if (it != chunks.end()) {
                chunk = &it->second;
            }
        }

        if (chunk) { // Process chunk outside the lock to minimize lock duration
            int localX = static_cast<int>(currentPos.x - chunk->chunkPosition.x);
            int localY = static_cast<int>(currentPos.y - chunk->chunkPosition.y);
            int localZ = static_cast<int>(currentPos.z - chunk->chunkPosition.z);

            if (localX >= 0 && localX < Chunk::chunkSize &&
                localY >= 0 && localY < Chunk::chunkHeight &&
                localZ >= 0 && localZ < Chunk::chunkSize) {

                size_t index = chunk->getBlockIndex(localX, localY, localZ);
                if (chunk->blocks[index] != BlockType::AIR) { // Fixed typo
                    highlightedBlockPos = glm::vec3(floor(currentPos.x), floor(currentPos.y), floor(currentPos.z));
                    hasHighlightedBlock = true;
                    glm::vec3 prevPos = rayOrigin + rayDir * (t - step);
                    prevBlock = glm::vec3(floor(prevPos.x), floor(prevPos.y), floor(prevPos.z));
                    return;
                }
            }
        }
    }
}

void Main::placeBlock() {

    if (hasHighlightedBlock) {
        glm::vec3 placePos = prevBlock; // Place block on the face indicated by the normal    

        if (player->blockIntersects(placePos)) {
            return; // Cancel placement if a player is in the way
        }   
       
        glm::vec3 chunkPos = glm::floor(placePos / glm::vec3(Chunk::chunkSize, 1, Chunk::chunkSize)) * glm::vec3(Chunk::chunkSize, 0, Chunk::chunkSize);
        chunkPos.y = -Chunk::baseTerrainHeight; // Adjust if your chunks have a fixed Y

        uint64_t key = getChunkKey(chunkPos.x/ CHUNK_SIZE, chunkPos.z/ CHUNK_SIZE);
        // Look up the chunk
        auto it = chunks.find(key);
        if (it != chunks.end()) {
            Chunk& chunk = it->second;

            // Calculate local coordinates within the chunk
            int localX = static_cast<int>(placePos.x - chunkPos.x);
            int localY = static_cast<int>(placePos.y - chunkPos.y);
            int localZ = static_cast<int>(placePos.z - chunkPos.z);

            // Check if the local coordinates are within chunk bounds
            if (localX >= 0 && localX < Chunk::chunkSize &&
                localY >= 0 && localY < Chunk::chunkHeight &&
                localZ >= 0 && localZ < Chunk::chunkSize) {

                // Directly set the block at the calculated local position
                chunk.setBlock(localX, localY, localZ, BlockType::STONE);
            }
        }
    }
}

void Main::breakBlock() { 
    if (hasHighlightedBlock) {


        glm::vec3 chunkPos = glm::floor(highlightedBlockPos / glm::vec3(Chunk::chunkSize, 1, Chunk::chunkSize)) * glm::vec3(Chunk::chunkSize, 0, Chunk::chunkSize);
        chunkPos.y = -Chunk::baseTerrainHeight; // Adjust if your chunks have a fixed Y

        uint64_t key = getChunkKey(chunkPos.x/CHUNK_SIZE, chunkPos.z/ CHUNK_SIZE);
        // Look up the chunk
        auto it = chunks.find(key);;
        if (it != chunks.end()) {
            Chunk& chunk = it->second;
            // Calculate local coordinates
            int localX = static_cast<int>(highlightedBlockPos.x - chunkPos.x);
            int localY = static_cast<int>(highlightedBlockPos.y - chunkPos.y);
            int localZ = static_cast<int>(highlightedBlockPos.z - chunkPos.z);

            // Check bounds and break the block
            if (localX >= 0 && localX < Chunk::chunkSize &&
                localY >= 0 && localY < Chunk::chunkHeight &&
                localZ >= 0 && localZ < Chunk::chunkSize) {
                chunk.setBlock(localX, localY, localZ, BlockType::AIR);
            }
        } 
    }
}

// Launch an asynchronous mesh update for a chunk.
void Main::updateChunkMeshAsync(Chunk& chunk) { 


    if (chunk.fullRebuildNeeded && chunk.isActive && activeAsyncTasks < MAX_ASYNC_TASKS()) {
        // Launch an async task that calls generateMeshData.
        chunkMeshFutures[chunk.chunkPosition] = std::async(std::launch::async, [&chunk]() {
            return chunk.generateMeshData();
            });
        // Reset the flags on the chunk so we don’t launch it again until needed.
        chunk.fullRebuildNeeded = false;
        activeAsyncTasks++; 
    }
}

// Check if an asynchronous mesh update for the given chunk is ready,and if so, update its OpenGL buffers.
void Main::tryApplyChunkMeshUpdate(Chunk& chunk) {
    auto it = chunkMeshFutures.find(chunk.chunkPosition);
    if (it != chunkMeshFutures.end()) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

            MeshData newMesh = it->second.get();
            chunkMeshFutures.erase(it);
            activeAsyncTasks--;

            chunk.verticesByType = std::move(newMesh.packedVerticesByType);
            chunk.indicesByType = std::move(newMesh.indicesByType);
            chunk.baseIndicesByType = std::move(newMesh.baseIndicesByType);

            std::vector<PackedVertex> allVertices;
            std::vector<unsigned int> allIndices;
            unsigned int vertexOffset = 0;
            unsigned int indexOffset = 0;
            for (auto& pair : chunk.verticesByType) {
                BlockType type = pair.first;
                chunk.baseIndicesByType[type] = indexOffset;
                allVertices.insert(allVertices.end(), pair.second.begin(), pair.second.end());
                for (unsigned int idx : chunk.indicesByType[type]) {
                    allIndices.push_back(idx + vertexOffset);
                }
                vertexOffset += pair.second.size();
                indexOffset += chunk.indicesByType[type].size();
            }

            // Check if buffers are valid
            if (chunk.VAO == 0 || chunk.VBO == 0 || chunk.EBO == 0) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "Invalid VAO/VBO/EBO for chunk " << glm::to_string(chunk.chunkPosition) << std::endl;
                return;
            }

            glBindVertexArray(chunk.VAO);
            glBindBuffer(GL_ARRAY_BUFFER, chunk.VBO);


            glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(PackedVertex), allVertices.data(), GL_DYNAMIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(unsigned int), allIndices.data(), GL_DYNAMIC_DRAW);


            
            glVertexAttribIPointer(0, 3, GL_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, pos)); 
            glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, colour)); 
            glVertexAttribIPointer(2, 2, GL_UNSIGNED_SHORT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, tex)); 
            glVertexAttribIPointer(3, 1, GL_INT, sizeof(PackedVertex), (void*)offsetof(PackedVertex, normal)); 

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glEnableVertexAttribArray(3);
             

            glBindVertexArray(0);


            chunk.fullRebuildNeeded = false;                        
        }
    }
}


void Main::processChunkMeshingInOrder() {

    std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Single lock for all chunk operations
    int processedChunks = 0;
    const int maxChunksPerFrame = 20;//needs to be >1 as 1 is always used for current chunk

    //pos in chunks (eg 0,0,1)
    glm::ivec3 playerChunkPos = glm::ivec3(
        floor(player->getCameraPos().x / CHUNK_SIZE),
        0, // Y is fixed at 0 for chunk coordinates
        floor(player->getCameraPos().x / CHUNK_SIZE)
    );

    //pos in world space,locked to chunk ( 0,0,16)
    glm::vec3 playerChunkRealPos = glm::vec3(
        playerChunkPos.x * CHUNK_SIZE,
        -Chunk::baseTerrainHeight,
        playerChunkPos.z * CHUNK_SIZE
    );

    uint64_t key = getChunkKey(playerChunkRealPos.x, playerChunkRealPos.z);
    // Look up the chunk
    auto playerChunkIt = chunks.find(key);
     
    if (playerChunkIt != chunks.end() && processedChunks < maxChunksPerFrame) { 
        Chunk& playerChunk = playerChunkIt->second; 
        if (playerChunk.isActive) { 
            if (playerChunk.fullRebuildNeeded) {  
                updateChunkMeshAsync(playerChunk); 
            } 
            tryApplyChunkMeshUpdate(playerChunk);
            processedChunks++;
        }
    }

    for (auto& pair : chunks) {
        if (processedChunks >= maxChunksPerFrame) break;
        Chunk& chunk = pair.second;
        if (chunk.isActive) {
            if (chunk.fullRebuildNeeded) {
                updateChunkMeshAsync(chunk);
            }
            tryApplyChunkMeshUpdate(chunk); 
        }
    }  
}

void Main::run() {

   
    createSun();

    getTextures();  
    createShaders();
    createHighlight();

    initNoise();


    player->spawn(glm::vec3(10,200,10));
    lastFrame = glfwGetTime();

    // Main loop
    while (!glfwWindowShouldClose(window)) {

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Cap deltaTime to prevent large spikes
        const float maxDeltaTime = 1.0f / 60.0f;
        if (deltaTime > maxDeltaTime) {
            deltaTime = maxDeltaTime;
        }
         
        updateChunks(player->getCameraPos());
        tryApplyChunkGeneration();
        player->update(deltaTime, chunks); 
        processInput(window); 

        processChunkMeshingInOrder();

        render();
        doFps();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

   

}

int main()
{
    Main main;

    main.run();

    return 0;

    
}