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
#define RENDER_DISTANCE 8

FastNoiseLite Main::noiseGen;
std::unordered_map<glm::ivec2, float, IVec2Hash> Main::noiseCache; 


Main::Main() : window(nullptr),width(1280),height(720),player(nullptr)
                , isInitialLoading(true), currentLoadingRadius(1), maxLoadingRadius(RENDER_DISTANCE) {

    init();
    player = std::make_unique<Player>(window);  // Use smart pointer for automatic cleanup;//give window to player

    int range = CHUNK_SIZE * RENDER_DISTANCE;

    for (float x = -range; x < range; x++) {
        for (float z = -range; z < range; z++) {

            noiseCache[{x, z}] = noiseGen.GetNoise(x,z);  // Fixed value 
        }
    }
    
}

Main::~Main() {
    // Destructor: Clean up GLFW
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
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    //some opengl settings
    glEnable(GL_CULL_FACE);   // Enable face culling 
    glCullFace(GL_BACK);      // Cull back faces (only render front faces) 
    glFrontFace(GL_CCW);      // Define front faces as counterclockwise (CCW) 
    glEnable(GL_DEPTH_TEST); 
    glfwSwapInterval(0);// 1 = V-Sync on, 0 = V-Sync off 
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);//wireframe mode 

    
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
    lightShader = new Shader("lightVertexShader.glsl","lightFragShader.glsl");

    shader->use();  
    modelLocation = glGetUniformLocation(shader->ID, "model");
    viewLocation = glGetUniformLocation(shader->ID, "view");
    projectionLocation = glGetUniformLocation(shader->ID, "projection");
    textureLocation = glGetUniformLocation(shader->ID, "texture1");
    lightColourLoc = glGetUniformLocation(shader->ID, "lightColour");
    lightPosLoc = glGetUniformLocation(shader->ID, "lightPos"); // New
    sunDirLoc = glGetUniformLocation(shader->ID, "sunDirection");

    lightShader->use();
    lightModelLocation = glGetUniformLocation(lightShader->ID, "model");
    lightViewLocation = glGetUniformLocation(lightShader->ID, "view");
    lightProjectionLocation = glGetUniformLocation(lightShader->ID, "projection");

}

void Main::createCube() {

    float vertices[] = {
        // positions          // colors           // texture coords
        // Back face
        -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f,
        // Front face
        -0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
        // Left face
        -0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   1.0f, 0.0f,
        // Right face
         0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,
         // Bottom face
         -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
          0.5f, -0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
          0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,
          0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,
         -0.5f, -0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   0.0f, 0.0f,
         -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
         // Top face
         -0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
          0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,
          0.5f,  0.5f, -0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 1.0f,
          0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,
         -0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,
         -0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 0.0f,   0.0f, 0.0f
    };

    float normals[] = {
        // Back face (0, 0, -1)
        0.0f, 0.0f, -1.0f,  0.0f, 0.0f, -1.0f,  0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, -1.0f,  0.0f, 0.0f, -1.0f,  0.0f, 0.0f, -1.0f,
        // Front face (0, 0, 1)
        0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f,
        // Left face (-1, 0, 0)
        -1.0f, 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f,
        // Right face (1, 0, 0)
        1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
        // Bottom face (0, -1, 0)
        0.0f, -1.0f, 0.0f,  0.0f, -1.0f, 0.0f,  0.0f, -1.0f, 0.0f,
        0.0f, -1.0f, 0.0f,  0.0f, -1.0f, 0.0f,  0.0f, -1.0f, 0.0f,
        // Top face (0, 1, 0)
        0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f
    };

    //create vertex array object
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);//bind VAO


    //create vertex buffer object
    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);//basically make GL_ARRAY_BUFFER work as a reference to vbo
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);//copy data (vertices) into the buffers memory

    // Set the position attribute (first 3 floats in each vertex)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); 
    glEnableVertexAttribArray(0); 

    // Set the color attribute (next 3 floats in each vertex)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); 
    glEnableVertexAttribArray(1); 

    //set textue position
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); 
    glEnableVertexAttribArray(2); 

    //bind normals to cube
    glGenBuffers(1, &normalsVBO);
    glBindBuffer(GL_ARRAY_BUFFER, normalsVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(normals), normals, GL_STATIC_DRAW);

    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0); // Normal 
    glEnableVertexAttribArray(3); 

    //unblund vao
    glBindVertexArray(0);

}

void Main::createLight() {

    float vertices[] = {
        // positions   
        // Back face
        -0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f, 
        // Front face
        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  
        -0.5f,  0.5f,  0.5f,   
        -0.5f, -0.5f,  0.5f,  
        // Left face
        -0.5f,  0.5f,  0.5f,   
        -0.5f,  0.5f, -0.5f,   
        -0.5f, -0.5f, -0.5f,   
        -0.5f, -0.5f, -0.5f,   
        -0.5f, -0.5f,  0.5f,  
        -0.5f,  0.5f,  0.5f, 
        // Right face
         0.5f,  0.5f,  0.5f,   
         0.5f, -0.5f, -0.5f, 
         0.5f,  0.5f, -0.5f,  
         0.5f, -0.5f, -0.5f, 
         0.5f,  0.5f,  0.5f,  
         0.5f, -0.5f,  0.5f, 
         // Bottom face
         -0.5f, -0.5f, -0.5f,  
          0.5f, -0.5f, -0.5f, 
          0.5f, -0.5f,  0.5f,   
          0.5f, -0.5f,  0.5f,  
         -0.5f, -0.5f,  0.5f,
         -0.5f, -0.5f, -0.5f,   
         // Top face
         -0.5f,  0.5f, -0.5f,  
          0.5f,  0.5f,  0.5f,   
          0.5f,  0.5f, -0.5f,  
          0.5f,  0.5f,  0.5f,   
         -0.5f,  0.5f, -0.5f, 
         -0.5f,  0.5f,  0.5f
    };
    //create vertex array object
    glGenVertexArrays(1, &lightVAO);
    glBindVertexArray(lightVAO);//bind VAO


    //create vertex buffer object
    glGenBuffers(1, &lightVBO);
    glBindBuffer(GL_ARRAY_BUFFER, lightVBO);//basically make GL_ARRAY_BUFFER work as a reference to vbo
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);//copy data (vertices) into the buffers memory

    // Set the position attribute (first 3 floats in each vertex)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    //unblund vao
    glBindVertexArray(0);

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

void Main::render() {

    // set background & clear screen
    glClearColor(0.1, 0.4, 0.6, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

    //vie and projection matrices
    //lookat(position, target, up)
    glm::mat4 view = player->getViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)width / (float)height, 0.1f, 1000.0f);

    //use light shader for light
    lightShader->use();

    glm::mat4 lightModel = glm::mat4(1.0f); 
    lightModel = glm::translate(lightModel,mainLightPos); 
    lightModel = glm::scale(lightModel, glm::vec3(0.2f));
    //pass view and projection matrices to shader
    glUniformMatrix4fv(lightModelLocation, 1, GL_FALSE, glm::value_ptr(lightModel)); 
    glUniformMatrix4fv(lightViewLocation, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(lightProjectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    //draw light cube  
    glBindVertexArray(lightVAO); 
    glDrawArrays(GL_TRIANGLES, 0, 36); 
    glBindVertexArray(0); 


    // Rotate sun direction based on time
    float time = glfwGetTime();
    float angle = time * 0.5f; // Adjust speed (0.1f = slow rotation, increase for faster)
    glm::vec3 baseSunDirection = glm::vec3(0.0f, -1.0f, -1.0f); // Starting direction
    sunDirection = glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1.0f, 0.0f, 0.0f))) * baseSunDirection;
    sunDirection = glm::normalize(sunDirection); // Ensure it stays a unit vector 

    //use normal shader for cube + chunks
    shader->use(); 
    // pass view and projection matrices to shader
    glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    
    //model matrice for rotating cube
    glm::mat4 cubeModel = glm::mat4(1.0f);
    cubeModel = glm::rotate(cubeModel, (float)glfwGetTime() * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));

    
    glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(cubeModel)); 
    glUniform4f(lightColourLoc, 1.0f, 1.0f, 1.0f, 1.0f);//whiteLgiht
    
    glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDirection)); // Pass direction of sun
    //bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureMap[BlockType::DIRT]);
    glUniform1i(textureLocation, 0); 
    //draw the rotating cube   
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

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
}

void Main::addChunks() {

   
    
    for (int x = -1; x <1; x++) {
        for (int z = -1; z < 1; z++) {
            glm::vec3 chunkPos = glm::vec3(static_cast<int>(x * Chunk::chunkSize),
                -Chunk::baseTerrainHeight,
                static_cast<int>(z * Chunk::chunkSize));
            // Create a new chunk at (x, 0, z), gen mesh
            chunks.emplace(chunkPos,Chunk(chunkPos,seed,this));
            updateChunkMeshAsync(chunks.at(chunkPos));

            // Add a new model matrix for this chunk (initially an identity matrix)
            chunkModels.emplace_back(glm::translate(glm::mat4(1.0f),chunkPos));
        }
    }

}

void Main::generateChunk(const glm::vec3& pos) {


    // Create a new chunk at (x, 0, z), gen mesh
    chunks.emplace(pos, Chunk(pos, seed, this));
    chunks.at(pos).isActive = true; 

    chunkModels.clear();
    for (const auto& pair : chunks) { 
        chunkModels.emplace_back(glm::translate(glm::mat4(1.0f), pair.first)); 
    }
}

// In Main.cpp
void Main::generateChunkAsync(const glm::vec3& pos) {
    // Launch async task to generate the chunk
    chunkGenerationFutures[pos] = std::async(std::launch::async, [this, pos]() {
        Chunk chunk(pos, seed, this);
        return chunk;
        });
}

void Main::tryApplyChunkGeneration() {
    std::lock_guard<std::recursive_mutex> lock(chunksMutex);
    for (auto it = chunkGenerationFutures.begin(); it != chunkGenerationFutures.end();) {
        if (it->second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            
            Chunk chunk = it->second.get();
            glm::vec3 pos = chunk.chunkPosition;

            chunk.initializeBuffers(); 

            chunks.emplace(pos, std::move(chunk));
            
            chunkModels.emplace_back(glm::translate(glm::mat4(1.0f),pos));
            
            it = chunkGenerationFutures.erase(it);

            
        }
        else {
            ++it;
        }
    }
}

void Main::updateChunks(const glm::vec3& playerPosition) {
    // Calculate the player's chunk position in chunk coordinates
    glm::ivec3 playerChunkPos = glm::ivec3(
        floor(playerPosition.x / CHUNK_SIZE),
        0, // Y is fixed at 0 for chunk coordinates
        floor(playerPosition.z / CHUNK_SIZE)
    );

    // Use a vector of pairs to store chunk positions and their distances
    std::vector<std::pair<glm::ivec3, float> > chunkPositions;
    int effectiveRenderDistance = isInitialLoading ? currentLoadingRadius : RENDER_DISTANCE;
    for (int x = -effectiveRenderDistance; x <= effectiveRenderDistance; x++) {
        for (int z = -effectiveRenderDistance; z <= effectiveRenderDistance; z++) {
            glm::ivec3 chunkPos = playerChunkPos + glm::ivec3(x, 0, z); // Offset in chunk coordinates
            glm::vec3 realChunkPos = glm::vec3(chunkPos.x * CHUNK_SIZE, -Chunk::baseTerrainHeight, chunkPos.z * CHUNK_SIZE);
            // Use Manhattan distance for faster sorting (no square root needed)
            float distance = std::abs(realChunkPos.x - playerPosition.x) + std::abs(realChunkPos.z - playerPosition.z);
            chunkPositions.push_back(std::make_pair(chunkPos, distance));
        }
    }

    // Sort by distance to prioritize closer chunks
    std::sort(chunkPositions.begin(), chunkPositions.end(),
        [](const std::pair<glm::ivec3, float>& a, const std::pair<glm::ivec3, float>& b) {
            return a.second < b.second;
        });

    std::unordered_set<glm::ivec3, IVec3Hash> loadedChunks;
    int chunksGeneratedThisFrame = 0;
    const int maxChunksGeneratedPerFrame = isInitialLoading ? 2 : 1; // Load 2 chunks per frame during initial loading
    for (std::vector<std::pair<glm::ivec3, float> >::const_iterator it = chunkPositions.begin(); it != chunkPositions.end(); ++it) {
        if (chunksGeneratedThisFrame >= maxChunksGeneratedPerFrame) break;
        glm::ivec3 chunkPos = it->first;
        float distance = it->second;
        loadedChunks.insert(chunkPos);
        glm::vec3 realChunkPos = glm::vec3(chunkPos.x * CHUNK_SIZE, -Chunk::baseTerrainHeight, chunkPos.z * CHUNK_SIZE);
        if (chunks.find(realChunkPos) == chunks.end() && chunkGenerationFutures.find(realChunkPos) == chunkGenerationFutures.end()) {
            generateChunkAsync(realChunkPos);
            chunksGeneratedThisFrame++;
        }
    }

    // Update the loading radius
    if (isInitialLoading) {
        std::lock_guard<std::recursive_mutex> lock(chunksMutex);
        // Check if all chunks within the current radius are loaded
        int expectedChunks = (2 * currentLoadingRadius + 1) * (2 * currentLoadingRadius + 1);
        int loadedChunksCount = 0;
        for (const auto& pair : chunks) {
            glm::ivec3 chunkKey = glm::ivec3(
                pair.first.x / CHUNK_SIZE,
                0,
                pair.first.z / CHUNK_SIZE
            );
            int dx = std::abs(chunkKey.x - playerChunkPos.x);
            int dz = std::abs(chunkKey.z - playerChunkPos.z);
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
    for (std::unordered_map<glm::vec3, Chunk, Vec3Hash>::iterator pair = chunks.begin(); pair != chunks.end(); ++pair) {
        glm::ivec3 chunkKey = glm::ivec3(
            pair->first.x / CHUNK_SIZE,
            0,
            pair->first.z / CHUNK_SIZE
        );
        if (loadedChunks.find(chunkKey) == loadedChunks.end()) {
            pair->second.isActive = false;
        }
        else {
            pair->second.isActive = true;
        }
    }
}

Chunk* Main::getChunk(const glm::vec3& pos) {
    std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Keep this for external access
    auto it = chunks.find(pos);
    return (it != chunks.end()) ? &it->second : nullptr;
} 

void Main::drawChunks() {
    std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Separate lock for rendering
    for (const auto& pair : chunks) { 
        const glm::vec3& pos = pair.first;
        const Chunk& chunk = pair.second;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model)); 

        glBindVertexArray(chunk.VAO);
        for (const auto& typePair : chunk.verticesByType) {
            BlockType type = typePair.first;
            const auto& indices = chunk.indicesByType.at(type);

            if (indices.empty()) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureMap.at(type));
            glUniform1i(textureLocation, 0);

            glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT,
                (void*)(chunk.baseIndicesByType.at(type) * sizeof(unsigned int))); 
        }
        glBindVertexArray(0); 
    }
}

void Main::getTextures() {

    int width, height, nrChannels;

    // Load another texture for a different block type, e.g., "stone.jpg"
    unsigned char* data = stbi_load("../ResourceFiles/dirt.jpg", &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load dirt texture!" << std::endl;
        exit(-1);
    }
    GLuint dirtTex;
    glGenTextures(1, &dirtTex);
    glBindTexture(GL_TEXTURE_2D, dirtTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);

    // Store texture ID for "dirt" block
    textureMap[BlockType::DIRT] = dirtTex;


    data = stbi_load("../ResourceFiles/stone.jpg", &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load stone texture!" << std::endl;
        exit(-1);
    }
    GLuint stoneTex;
    glGenTextures(1, &stoneTex);
    glBindTexture(GL_TEXTURE_2D, stoneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);

    // Store texture ID for "dirt" block
    textureMap[BlockType::STONE] = stoneTex;

    data = stbi_load("../ResourceFiles/grass.jpg", &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load grass texture!" << std::endl;
        exit(-1);
    }
    GLuint grassTex;
    glGenTextures(1, &grassTex);
    glBindTexture(GL_TEXTURE_2D, grassTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);

    // Store texture ID for "dirt" block
    textureMap[BlockType::GRASS] = grassTex;
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
        auto it = chunks.find(chunkPos);
        if (it != chunks.end()) {
            Chunk& chunk = it->second;

            // Calculate local coordinates within the chunk
            int localX = static_cast<int>(currentPos.x - chunk.chunkPosition.x);
            int localY = static_cast<int>(currentPos.y - chunk.chunkPosition.y);
            int localZ = static_cast<int>(currentPos.z - chunk.chunkPosition.z);

            // Check if local coordinates are within chunk bounds
            if (localX >= 0 && localX < Chunk::chunkSize &&
                localY >= 0 && localY < Chunk::chunkHeight &&
                localZ >= 0 && localZ < Chunk::chunkSize) {

                // Look up the block using the local coordinates
                glm::ivec3 blockKey = glm::ivec3(localX, localY, localZ); 
                auto blockIt = chunk.blocks.find(blockKey); 
                if (blockIt != chunk.blocks.end() && blockIt->second != BlockType::AIR) { 
                    // Set highlighted block position
                    highlightedBlockPos = glm::vec3(floor(currentPos.x), floor(currentPos.y), floor(currentPos.z)); 
                    hasHighlightedBlock = true;

                    // Compute previous block position for normal calculation
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
        glm::vec3 chunkPos = glm::floor(placePos / glm::vec3(Chunk::chunkSize, 1, Chunk::chunkSize)) * glm::vec3(Chunk::chunkSize, 0, Chunk::chunkSize);
        chunkPos.y = -Chunk::baseTerrainHeight; // Adjust if your chunks have a fixed Y

        // Look for the chunk in the chunks map
        auto it = chunks.find(chunkPos);
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

        // Find the chunk in the map
        auto it = chunks.find(chunkPos);
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


    if (chunk.fullRebuildNeeded && chunk.isActive && activeAsyncTasks < MAX_ASYNC_TASKS) {
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
            try {
                MeshData newMesh = it->second.get();
                chunkMeshFutures.erase(it);
                activeAsyncTasks--;

                {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cout << "Completed async task for chunk " << glm::to_string(chunk.chunkPosition)
                        << ". Active tasks: " << activeAsyncTasks << std::endl;
                    std::cout << "Mesh data: " << newMesh.verticesByType.size() << " types, ";
                    for (const auto& pair : newMesh.indicesByType) {
                        std::cout << pair.second.size() << " indices for type " << static_cast<int>(pair.first) << ", ";
                    }
                    std::cout << std::endl;
                }

                chunk.verticesByType = std::move(newMesh.verticesByType);
                chunk.indicesByType = std::move(newMesh.indicesByType);
                chunk.baseIndicesByType = std::move(newMesh.baseIndicesByType);

                std::vector<float> allVertices;
                std::vector<unsigned int> allIndices;
                unsigned int vertexOffset = 0;
                unsigned int indexOffset = 0;
                for (auto& pair : chunk.verticesByType) {
                    BlockType type = pair.first;
                    chunk.baseIndicesByType[type] = indexOffset;
                    allVertices.insert(allVertices.end(), pair.second.begin(), pair.second.end());
                    for (unsigned int idx : chunk.indicesByType[type]) {
                        allIndices.push_back(idx + vertexOffset / 11);
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
                GLenum error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBindVertexArray: " << error << std::endl;
                }

                glBindBuffer(GL_ARRAY_BUFFER, chunk.VBO);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBindBuffer(GL_ARRAY_BUFFER): " << error << std::endl;
                }

                glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(float), allVertices.data(), GL_STATIC_DRAW);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBufferData(GL_ARRAY_BUFFER): " << error << std::endl;
                }

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.EBO);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBindBuffer(GL_ELEMENT_ARRAY_BUFFER): " << error << std::endl;
                }

                glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size() * sizeof(unsigned int), allIndices.data(), GL_STATIC_DRAW);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBufferData(GL_ELEMENT_ARRAY_BUFFER): " << error << std::endl;
                }

                // Changed normals to GL_FLOAT for compatibility
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after setting attribute 0: " << error << std::endl;
                }

                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
                glEnableVertexAttribArray(1);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after setting attribute 1: " << error << std::endl;
                }

                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
                glEnableVertexAttribArray(2);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after setting attribute 2: " << error << std::endl;
                }

                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(8 * sizeof(float)));
                glEnableVertexAttribArray(3);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after setting attribute 3: " << error << std::endl;
                }

                glBindVertexArray(0);
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    std::lock_guard<std::mutex> lock(logMutex);
                    std::cerr << "OpenGL Error after glBindVertexArray(0): " << error << std::endl;
                }

                chunk.fullRebuildNeeded = false;
            }


            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::cerr << "Error retrieving future for chunk " << glm::to_string(chunk.chunkPosition)
                    << ": " << e.what() << std::endl;
                chunkMeshFutures.erase(it);
                activeAsyncTasks--;
            }
        }
    }
}

void Main::run() {

    if (seed == -1) {
        std::random_device rd;
        seed = rd();
        std::cout << seed;
    }
    //create noise for world gen
    noiseGen.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noiseGen.SetFrequency(0.03f); // Lower frequency for smoother terrain
    noiseGen.SetSeed(seed);

    createCube();
    createLight();

    getTextures(); 
    //addChunks(); 
    createShaders();
    createHighlight();


    player->spawn(glm::vec3(10,200,10));
    lastFrame = glfwGetTime();

    // Main loop
    while (!glfwWindowShouldClose(window)) {

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Cap deltaTime to prevent large spikes (e.g., max 1/30 seconds)
        const float maxDeltaTime = 1.0f / 60.0f; // 30 FPS minimum
        if (deltaTime > maxDeltaTime) {
            deltaTime = maxDeltaTime;
        }
         
        updateChunks(player->getCameraPos());
        tryApplyChunkGeneration();
        player->update(deltaTime, chunks); 
        processInput(window); 

        {
            std::lock_guard<std::recursive_mutex> lock(chunksMutex); // Single lock for all chunk operations
            int processedChunks = 0;
            const int maxChunksPerFrame = 1;
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
        } // Mutex unlocked here

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