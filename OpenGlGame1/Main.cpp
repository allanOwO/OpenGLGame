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
#include <limits>

#define CHUNK_SIZE 16
#define RENDER_DISTANCE 16
#define UNLOAD_MARGIN 2 // extra chunks kept past render distance before unloading
#define SUN_TILT glm::radians(70.0f)
#define SUN_SPEED 0.1f
#define PHYSICS_STEP (1.0f / 120.0f) // seconds per physics update

FastNoiseLite Main::noiseGen;
//std::unordered_map<glm::ivec2, float, IVec2Hash> Main::noiseCache; 


Main::Main() : window(nullptr),width(1280),height(720),player(nullptr)
                , isInitialLoading(true), currentLoadingRadius(0), maxLoadingRadius(RENDER_DISTANCE) {

    init();
    player = std::make_unique<Player>(window,this);  // Use smart pointer for automatic cleanup;//give window to player 
}

Main::~Main() {
    player.reset();

    // Wait for worker threads, then free GL objects while the context still exists
    chunkGenerationFutures.clear();
    chunkMeshFutures.clear();
    chunks.clear();
    delete shader;
    delete sunShader;
    delete depthShader;
    delete highlightShader;

    glfwDestroyWindow(window);
    glfwTerminate();
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
    glfwSetWindowUserPointer(window, this);
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


    //test wihtout initial cache
    /*
    * int range = CHUNK_SIZE * RENDER_DISTANCE;

    for (int x = -range; x < range; x++) {
        for (int z = -range; z < range; z++) {

            noiseCache[{x, z}] = getNoise(static_cast<float>(x), static_cast<float>(z));  // Fixed value 
        }
    }
    
    */
    
}

float Main::getNoise(float x, float z) const{

    //test no cache
    /*
    glm::ivec2 noiseKey(x, z);

    if (noiseCache.find(noiseKey) != noiseCache.end()) {
        return noiseCache[noiseKey];
    }
    else {
        float biomeValue = getBiomeNoise(x, z); // Low-frequency noise for biome selection
        float height = remapHeight(getWarpedHeight(x, z,biomeValue), biomeValue);
        noiseCache[noiseKey] = height;
        return height;
    }
    */
    
    float biomeValue = getBiomeNoise(x, z);
    float warpedHeight = getWarpedHeight(x, z, biomeValue);
    return remapHeight(warpedHeight, biomeValue);
}

// New function: Low-frequency noise to determine biome type
float Main::getBiomeNoise(float x, float z) const{
    float biomeScale = 0.002f; // Very low frequency for large biome areas
    float biomeNoise = Main::noiseGen.GetNoise(x * biomeScale, z * biomeScale);
    //return 1;
    return (biomeNoise + 1.0f) / 2.0f; // Normalize to 0-1
    
}

/// How much of each biome applies, smoothstep keeps the blend continuous so there are no cliffs at biome edges
static float hillsWeight(float biomeValue) { return glm::smoothstep(0.25f, 0.45f, biomeValue); }
static float mountainsWeight(float biomeValue) { return glm::smoothstep(0.55f, 0.75f, biomeValue); }

// pow that keeps the sign, so values under "flatness" curve down instead of mirroring back up
static float signedPow(float value, float exponent) {
    return value < 0.0f ? -pow(-value, exponent) : pow(value, exponent);
}

//takes 0 to 1 noise value and assigns height in y
float Main::remapHeight(float noiseValue, float biomeValue) const {
    float minHeight = 32.0f;  // Base height
    float maxHeight = 150.0f; // Max height (can increase for taller mountains)

    // Define height functions for each biome
    auto plainsHeight = [](float nv) {
        float flatness = 0.8f; // Keep plains flattish
        return flatness * nv;
        };

    auto hillsHeight = [](float nv) {
        float flatness = 0.5f;
        float steepness = 2.0f;
        return flatness + (1.0f - flatness) * signedPow((nv - flatness) / (1.0f - flatness), steepness);
        };

    auto mountainsHeight = [](float nv) {
        float flatness = 0.3f; // Less flat area for mountains
        float steepness = 3.0f; // Steeper slopes
        return flatness + (1.0f - flatness) * signedPow((nv - flatness) / (1.0f - flatness), steepness);
        };

    // Blend plains -> hills -> mountains
    float hills = hillsWeight(biomeValue);
    float mountains = mountainsWeight(biomeValue);
    float height = glm::mix(plainsHeight(noiseValue), hillsHeight(noiseValue), hills);
    height = glm::mix(height, mountainsHeight(noiseValue), mountains);

    // Scale height based on biome, mountains top out just under the chunk height
    float mountainScale = (Chunk::chunkHeight - 1 - minHeight) / maxHeight;
    float scale = glm::mix(0.2f, 0.8f, hills);
    scale = glm::mix(scale, mountainScale, mountains);

    float finalHeight = minHeight + height * (maxHeight * scale);
    return glm::clamp(finalHeight, 0.0f, static_cast<float>(Chunk::chunkHeight - 1));
}

// Layered noise (fractal brownian motion) normalised to 0-1
static float fbm(const FastNoiseLite& noise, float x, float z, float frequency, float persistence, int octaves) {
    float height = 0.0f;
    float amplitude = 0.9f;
    float lacunarity = 2.0f;
    float totalAmplitude = 0.0f;

    for (int i = 0; i < octaves; i++) {
        height += noise.GetNoise(x * frequency, z * frequency) * amplitude;
        totalAmplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return (height + totalAmplitude) / (2.0f * totalAmplitude);
}

// Updated getWarpedHeight for more variation
float Main::getWarpedHeight(float x, float z, float biomeValue) const{
    float warpScale = 0.2f;
    float warpX = Main::noiseGen.GetNoise(x * warpScale + 10.0f, z * warpScale + 10.0f) * 15.0f; // Increased warp
    float warpZ = Main::noiseGen.GetNoise(x * warpScale + 20.0f, z * warpScale + 20.0f) * 15.0f;
    float warpedX = x + warpX;
    float warpedZ = z + warpZ;

    // Mountains use lower frequency, more octaves (jaggedness) and exaggerated peaks
    // Both are worked out and blended in the transition so the terrain doesn't jump
    float mountains = mountainsWeight(biomeValue);
    float lowland = 0.0f, highland = 0.0f;
    if (mountains < 1.0f) {
        lowland = fbm(noiseGen, warpedX, warpedZ, 0.3f, 0.6f, 4);
    }
    if (mountains > 0.0f) {
        highland = pow(fbm(noiseGen, warpedX, warpedZ, 0.08f, 0.7f, 5), 0.6f);
    }

    return glm::mix(lowland, highland, mountains);
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
    shader = new Shader(getResourcePath("vertex_shader.glsl").c_str(), getResourcePath("fragment_shader.glsl").c_str());
    sunShader = new Shader(getResourcePath("sunVertex.glsl").c_str(), getResourcePath("sunFragment.glsl").c_str());
    depthShader = new Shader(getResourcePath("depth_vertex.glsl").c_str(), getResourcePath("depth_fragment.glsl").c_str());
    highlightShader = new Shader(getResourcePath("highlight_vertex.glsl").c_str(), getResourcePath("highlight_fragment.glsl").c_str());

    highlightModelLoc = glGetUniformLocation(highlightShader->ID, "model");
    highlightViewLoc = glGetUniformLocation(highlightShader->ID, "view");
    highlightProjLoc = glGetUniformLocation(highlightShader->ID, "projection");
    highlightColourLoc = glGetUniformLocation(highlightShader->ID, "colour");

    if (sunShader->ID == 0) {
        std::cerr << "Sun shader failed to compile/link" << std::endl;
    }
    std::cout << "Sun shader ID: " << sunShader->ID << std::endl; // Verify ID 

    shader->use();  
    modelLocation = glGetUniformLocation(shader->ID, "model");
    viewLocation = glGetUniformLocation(shader->ID, "view");
    projectionLocation = glGetUniformLocation(shader->ID, "projection");
    textureLocation = glGetUniformLocation(shader->ID, "ourTexture");
    sunDirLoc = glGetUniformLocation(shader->ID, "sunDirection");
    camPosLoc = glGetUniformLocation(shader->ID, "cameraPos");
    shadowMapLoc = glGetUniformLocation(shader->ID, "shadowMap");
    mainLightSpaceLoc = glGetUniformLocation(shader->ID, "lightSpaceMatrix");

    // Samplers never change unit, set once: atlas on 0, shadow map on 1
    glUniform1i(textureLocation, 0);
    glUniform1i(shadowMapLoc, 1);

    sunShader->use();
    sunViewLoc = glGetUniformLocation(sunShader->ID, "view");
    sunProjLoc = glGetUniformLocation(sunShader->ID, "projection");
    sunSunDirLoc = glGetUniformLocation(sunShader->ID, "sunDirection");
    sunColourLoc = glGetUniformLocation(sunShader->ID, "sunColour");
    sunCamPosLoc = glGetUniformLocation(sunShader->ID, "camPos");

    depthShader->use();
    lightSpaceLoc = glGetUniformLocation(depthShader->ID, "lightSpaceMatrix");
    depthModelLoc = glGetUniformLocation(depthShader->ID, "model");

    if (lightSpaceLoc == -1) {
        std::cerr << "Uniform 'lightSpaceMatrix' not found in depth shader" << std::endl;
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
    // Render depth map from the sun's point of view
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glDepthMask(GL_TRUE); // Ensure depth writes are enabled
    glClear(GL_DEPTH_BUFFER_BIT);

    depthShader->use();
    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    // Only chunks inside the light's box can cast shadows we see
    Frustum lightFrustum;
    lightFrustum.update(lightSpaceMatrix);

    //render chunks 1st pass to create shadows
    for (const auto& pair : chunks) {
        const Chunk& chunk = pair.second;
        if (!chunk.isActive || chunk.indexCount == 0) continue;

        glm::vec3 min = chunk.chunkPosition;
        glm::vec3 max = min + glm::vec3(CHUNK_SIZE, chunk.currentTallestBlock + 1, CHUNK_SIZE);
        if (!lightFrustum.isBoxInFrustum(min, max)) continue;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), chunk.chunkPosition);
        glUniformMatrix4fv(depthModelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(chunk.VAO);
        glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Main::drawChunks() { 
    shader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    glUniform1i(textureLocation, 0);

    for (const auto& pair : chunks) {
        
        const Chunk& chunk = pair.second;
        if (!chunk.isActive || chunk.indexCount == 0)
            continue;

        const glm::vec3& pos = chunk.chunkPosition;

        // Frustum culling 
        glm::vec3 min = pos;
        glm::vec3 max = pos + glm::vec3(CHUNK_SIZE, chunk.currentTallestBlock+1, CHUNK_SIZE);
        if (!frustum.isBoxInFrustum(min, max)) {
            continue; // Skip chunks outside the frustum 
        }

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));

        // whole chunk is one buffer, all blocks share the atlas
        glBindVertexArray(chunk.VAO);
        glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
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
    //shadow camera sits towards the sun and looks at the player, the box is centred on the player
    glm::vec3 playerPos = player->getCameraPos();
    glm::mat4 lightProjection = glm::ortho(-SHADOW_RADIUS, SHADOW_RADIUS, -SHADOW_RADIUS, SHADOW_RADIUS,
        1.0f, SHADOW_LIGHT_DISTANCE * 2.0f);
    glm::vec3 lightPos = playerPos + sunDir * SHADOW_LIGHT_DISTANCE;
    glm::mat4 lightView = glm::lookAt(lightPos, playerPos, glm::vec3(0.0, 1.0, 0.0));

    // Snap the box to whole shadow map texels so shadow edges don't shimmer as the player moves
    glm::mat4 shadowMatrix = lightProjection * lightView;
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float texelsPerUnit = SHADOW_WIDTH * 0.5f;
    glm::vec2 snapOffset = (glm::round(glm::vec2(shadowOrigin) * texelsPerUnit) - glm::vec2(shadowOrigin) * texelsPerUnit) / texelsPerUnit;
    lightProjection[3][0] += snapOffset.x;
    lightProjection[3][1] += snapOffset.y;
    lightSpaceMatrix = lightProjection * lightView;

    // Below the horizon the shader ignores sunlight, so shadows wouldn't show anyway
    bool sunUp = sunDir.y > -0.1f;
    if (sunUp) {
        renderShadowMap();
    }

    // Back to the default framebuffer for the main pass
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    //use normal shader for cube + chunks
    shader->use();

    // pass view and projection matrices to shader
    glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(camPosLoc, 1, glm::value_ptr(player->getCameraPos()));
    glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDirection));
    glUniformMatrix4fv(mainLightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    // Bind shadow map to texture unit 1 (unit 0 is for texture atlas, bound in drawChunks)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthMap);

    drawChunks();

    raycastBlock();//used to find currently faced blocl
    // Render highlight if a block is in range
    if (hasHighlightedBlock) {
        highlightShader->use();
        // Center the highlight by adding (0.5, 0.5, 0.5) to the block position
        glm::mat4 highlightModel = glm::translate(glm::mat4(1.0f), highlightedBlockPos + glm::vec3(0.5f, 0.5f, 0.5f));
        glUniformMatrix4fv(highlightModelLoc, 1, GL_FALSE, glm::value_ptr(highlightModel));
        glUniformMatrix4fv(highlightViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(highlightProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform4f(highlightColourLoc, 1.0f, 1.0f, 0.0f, 1.0f); // Yellow highlight

        glBindVertexArray(highlightVAO);
        glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0); // 24 indices for wireframe
        glBindVertexArray(0);
    }

    renderSun(view,projection,-sunDirection);
}

static int floorDiv(int a, int b) {
    return static_cast<int>(std::floor(static_cast<float>(a) / b));
}

void Main::generateChunkAsync(const glm::vec3& pos) {
    // Launch async task to generate the chunk, the chunk is only added to the map on the main thread
    int chunkX = static_cast<int>(floor(pos.x / CHUNK_SIZE));
    int chunkZ = static_cast<int>(floor(pos.z / CHUNK_SIZE));
    uint64_t key = getChunkKey(chunkX, chunkZ);

    chunkGenerationFutures[key] = std::async(std::launch::async, [this, pos]() {
        return Chunk(pos, this);
        });
}

void Main::tryApplyChunkGeneration() {
    for (auto it = chunkGenerationFutures.begin(); it != chunkGenerationFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

            uint64_t key = it->first; // The key is the uint64_t chunk identifier
            auto inserted = chunks.emplace(key, it->second.get());
            Chunk& chunk = inserted.first->second;

            // Initialize buffers for rendering
            chunk.initializeBuffers();

            // Neighbours meshed without this chunk have faces on the shared border, rebuild them
            markNeighboursForRebuild(chunkKeyX(key), chunkKeyZ(key));

            // Remove the completed future
            it = chunkGenerationFutures.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Main::markNeighboursForRebuild(int chunkX, int chunkZ) {
    const glm::ivec2 offsets[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    for (const auto& offset : offsets) {
        if (Chunk* neighbour = findChunk(chunkX + offset.x, chunkZ + offset.y)) {
            neighbour->fullRebuildNeeded = true;
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

    for (const auto& pair : chunkPositions) {
        loadedChunks.insert(pair.first);
        if (chunksGeneratedThisFrame >= maxChunksGeneratedPerFrame) continue;

        glm::vec3 realChunkPos(chunkKeyX(pair.first) * CHUNK_SIZE, -Chunk::baseTerrainHeight, chunkKeyZ(pair.first) * CHUNK_SIZE);

        if (chunks.find(pair.first) == chunks.end() && chunkGenerationFutures.find(pair.first) == chunkGenerationFutures.end()) {
            generateChunkAsync(realChunkPos);
            chunksGeneratedThisFrame++;
        }
    }

    // Update the loading radius
    if (isInitialLoading) {
        // Check if all chunks within the current radius are loaded
        int expectedChunks = (2 * currentLoadingRadius + 1) * (2 * currentLoadingRadius + 1);
        int loadedChunksCount = 0;
        for (const auto& pair : chunks) {
            int dx = std::abs(chunkKeyX(pair.first) - playerChunkPos.x);
            int dz = std::abs(chunkKeyZ(pair.first) - playerChunkPos.z);
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
    for (auto& pair : chunks) {
        pair.second.isActive = loadedChunks.find(pair.first) != loadedChunks.end();
    }

    unloadFarChunks(playerChunkPos);
}

void Main::unloadFarChunks(const glm::ivec3& playerChunkPos) {
    // Margin stops chunks on the edge being unloaded and reloaded when walking back and forth
    const int unloadDistance = RENDER_DISTANCE + UNLOAD_MARGIN;
    for (auto it = chunks.begin(); it != chunks.end();) {
        int dx = std::abs(chunkKeyX(it->first) - playerChunkPos.x);
        int dz = std::abs(chunkKeyZ(it->first) - playerChunkPos.z);
        if (dx > unloadDistance || dz > unloadDistance) {
            // Any mesh job still running for it only has a copy, its result is dropped when it finishes
            it = chunks.erase(it);
        }
        else {
            ++it;
        }
    }
}

Chunk* Main::getChunk(const glm::vec3& pos) {
    return findChunk(static_cast<int>(floor(pos.x / CHUNK_SIZE)), static_cast<int>(floor(pos.z / CHUNK_SIZE)));
}

Chunk* Main::findChunk(int chunkX, int chunkZ) {
    auto it = chunks.find(getChunkKey(chunkX, chunkZ));
    return (it != chunks.end()) ? &it->second : nullptr;
}

BlockType Main::getWorldBlock(const glm::ivec3& worldPos) {
    int localY = worldPos.y + Chunk::baseTerrainHeight;
    if (localY < 0 || localY >= Chunk::chunkHeight) return BlockType::AIR;

    int chunkX = floorDiv(worldPos.x, CHUNK_SIZE);
    int chunkZ = floorDiv(worldPos.z, CHUNK_SIZE);
    Chunk* chunk = findChunk(chunkX, chunkZ);
    if (!chunk) return BlockType::AIR;

    return chunk->getBlock(worldPos.x - chunkX * CHUNK_SIZE, localY, worldPos.z - chunkZ * CHUNK_SIZE);
}

void Main::setWorldBlock(const glm::ivec3& worldPos, BlockType type) {
    int localY = worldPos.y + Chunk::baseTerrainHeight;
    if (localY < 0 || localY >= Chunk::chunkHeight) return;

    int chunkX = floorDiv(worldPos.x, CHUNK_SIZE);
    int chunkZ = floorDiv(worldPos.z, CHUNK_SIZE);
    Chunk* chunk = findChunk(chunkX, chunkZ);
    if (!chunk) return;

    int localX = worldPos.x - chunkX * CHUNK_SIZE;
    int localZ = worldPos.z - chunkZ * CHUNK_SIZE;
    chunk->setBlock(localX, localY, localZ, type);

    // Blocks on the edge change the neighbour's visible faces too
    auto rebuild = [this](int x, int z) {
        if (Chunk* neighbour = findChunk(x, z)) neighbour->fullRebuildNeeded = true;
    };
    if (localX == 0) rebuild(chunkX - 1, chunkZ);
    if (localX == CHUNK_SIZE - 1) rebuild(chunkX + 1, chunkZ);
    if (localZ == 0) rebuild(chunkX, chunkZ - 1);
    if (localZ == CHUNK_SIZE - 1) rebuild(chunkX, chunkZ + 1);
}



void Main::getTextures() {

    int width, height, nrChannels;

    // Load another texture for a different block type, e.g., "stone.jpg"
    unsigned char* data = stbi_load(getResourcePath("textureAtlas.png").c_str(), &width, &height, &nrChannels, 0);
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
 
std::string Main::getResourcePath(const std::string& subPath) {
    // 1. Check current directory (For the Release .exe)
    if (std::filesystem::exists("ResourceFiles/" + subPath)) {
        return "ResourceFiles/" + subPath;
    }
    // 2. Check one level up (For Visual Studio F5/Debug mode)
    if (std::filesystem::exists("../ResourceFiles/" + subPath)) {
        return "../ResourceFiles/" + subPath;
    }

    // Fallback: return the path as-is
    return "ResourceFiles/" + subPath;
}

void Main::doFps() {
    float currentTime = glfwGetTime();
    frameCount++;

    // Update FPS every second
    if (currentTime - lastFPSTime >= 1.0f) {
        fps = frameCount / (currentTime - lastFPSTime);
        frameCount = 0;
        lastFPSTime = currentTime;

        glm::ivec3 position = player->getCameraPos(); 

        // Update window title with FPS
        std::string title = "My Game - FPS: " + std::to_string(fps)
            + " Position: "
            + "X:" + std::to_string(position.x)
            + " Y:"+ std::to_string(position.y)
            + " Z:"+ std::to_string(position.z);

        glfwSetWindowTitle(window, title.c_str());
    }
}
 

void Main::raycastBlock() {
    // Voxel traversal (Amanatides & Woo), visits every block the ray passes through in order
    hasHighlightedBlock = false;
    glm::vec3 rayOrigin = player->getCameraPos();
    glm::vec3 rayDir = glm::normalize(player->getCameraFront());

    glm::ivec3 block = glm::ivec3(glm::floor(rayOrigin));
    glm::ivec3 step(0);
    glm::vec3 tMax(std::numeric_limits<float>::infinity());//ray distance to the next block boundary on each axis
    glm::vec3 tDelta(std::numeric_limits<float>::infinity());//ray distance to cross one whole block on each axis

    for (int i = 0; i < 3; i++) {
        if (rayDir[i] > 0.0f) {
            step[i] = 1;
            tMax[i] = (block[i] + 1.0f - rayOrigin[i]) / rayDir[i];
            tDelta[i] = 1.0f / rayDir[i];
        }
        else if (rayDir[i] < 0.0f) {
            step[i] = -1;
            tMax[i] = (rayOrigin[i] - block[i]) / -rayDir[i];
            tDelta[i] = -1.0f / rayDir[i];
        }
    }

    glm::ivec3 previous = block;
    float t = 0.0f;
    while (t <= reachDistance) {
        if (getWorldBlock(block) != BlockType::AIR) {
            highlightedBlockPos = glm::vec3(block);
            prevBlock = glm::vec3(previous);//always shares a face with the hit block
            hasHighlightedBlock = true;
            return;
        }

        // Step along whichever axis reaches its next boundary first
        int axis = (tMax.x < tMax.y) ? ((tMax.x < tMax.z) ? 0 : 2) : ((tMax.y < tMax.z) ? 1 : 2);
        previous = block;
        t = tMax[axis];
        block[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
}

void Main::placeBlock() {

    if (hasHighlightedBlock) {
        glm::vec3 placePos = prevBlock; // Empty block next to the hit face

        if (player->blockIntersects(placePos)) {
            return; // Cancel placement if a player is in the way
        }

        setWorldBlock(glm::ivec3(placePos), BlockType::STONE);
    }
}

void Main::breakBlock() {
    if (hasHighlightedBlock) {
        setWorldBlock(glm::ivec3(highlightedBlockPos), BlockType::AIR);
    }
}

// Copy the chunk and its neighbour borders, then build the mesh on a worker thread
void Main::launchChunkMesh(uint64_t key, Chunk& chunk) {
    int chunkX = chunkKeyX(key);
    int chunkZ = chunkKeyZ(key);
    const Chunk* neighbours[4] = {
        findChunk(chunkX + 1, chunkZ), // +X
        findChunk(chunkX - 1, chunkZ), // -X
        findChunk(chunkX, chunkZ + 1), // +Z
        findChunk(chunkX, chunkZ - 1)  // -Z
    };

    ChunkMeshInput input = chunk.makeMeshInput(neighbours);
    // Cleared before the job runs, so edits made while it runs queue another rebuild
    chunk.fullRebuildNeeded = false;

    chunkMeshFutures[key] = std::async(std::launch::async, [input = std::move(input)]() {
        return Chunk::buildMesh(input);
        });
}

void Main::processChunkMeshing() {
    // Upload finished meshes
    for (auto it = chunkMeshFutures.begin(); it != chunkMeshFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        MeshData mesh = it->second.get();
        if (Chunk* chunk = findChunk(chunkKeyX(it->first), chunkKeyZ(it->first))) {
            chunk->uploadMesh(mesh);
        }// else the chunk was unloaded while meshing
        it = chunkMeshFutures.erase(it);
    }

    if (chunkMeshFutures.size() >= MAX_ASYNC_TASKS()) return;
    size_t freeSlots = MAX_ASYNC_TASKS() - chunkMeshFutures.size();

    // Chunks waiting for a mesh, closest to the player first
    glm::vec3 playerPos = player->getCameraPos();
    std::vector<std::pair<float, uint64_t>> waiting;
    for (auto& pair : chunks) {
        const Chunk& chunk = pair.second;
        if (!chunk.isActive || !chunk.fullRebuildNeeded) continue;
        if (chunkMeshFutures.find(pair.first) != chunkMeshFutures.end()) continue;//one job per chunk at a time

        float dx = chunk.chunkPosition.x + CHUNK_SIZE * 0.5f - playerPos.x;
        float dz = chunk.chunkPosition.z + CHUNK_SIZE * 0.5f - playerPos.z;
        waiting.emplace_back(dx * dx + dz * dz, pair.first);
    }

    size_t launchCount = std::min(freeSlots, waiting.size());
    std::partial_sort(waiting.begin(), waiting.begin() + launchCount, waiting.end());
    for (size_t i = 0; i < launchCount; i++) {
        uint64_t key = waiting[i].second;
        launchChunkMesh(key, chunks.at(key));
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

        // Cap deltaTime so a long stall (window drag, breakpoint) doesn't run hundreds of physics steps
        const float maxDeltaTime = 0.25f;
        if (deltaTime > maxDeltaTime) {
            deltaTime = maxDeltaTime;
        }

        updateChunks(player->getCameraPos());
        tryApplyChunkGeneration();

        // Physics runs in fixed steps so movement is the same at any frame rate
        physicsAccumulator += deltaTime;
        while (physicsAccumulator >= PHYSICS_STEP) {
            player->update(PHYSICS_STEP, chunks);
            physicsAccumulator -= PHYSICS_STEP;
        }
        // Camera blends between the last two steps so it stays smooth above the physics rate
        player->interpolate(physicsAccumulator / PHYSICS_STEP);

        processInput(window);

        processChunkMeshing();

        render();
        doFps();

#ifdef _DEBUG
        // One check per frame, glGetError after every call stalls the GPU
        for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) {
            std::cerr << "OpenGL error this frame: 0x" << std::hex << err << std::dec << std::endl;
        }
#endif

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