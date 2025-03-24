#include <iostream>
#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include "Main.h"
#define STB_IMAGE_IMPLEMENTATION  // This tells stb to include the implementation
#include "stb/stb_image.h"          // Path to the stb_image.h file

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include<random>
#include<memory>



Main::Main() : window(nullptr),width(1280),height(720),player(nullptr){

    init();
  

    player = std::make_unique<Player>(window);  // Use smart pointer for automatic cleanup;//give window to player
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
    glfwSwapInterval(1);// 1 = V-Sync on, 0 = V-Sync off 
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

    if (seed == -1) {
        std::random_device rd;
        seed = rd();
        std::cout << seed;
    }
    int chunkCount = 0;
    
    for (int x = -8; x <8; x++) {
        for (int z = -8; z < 8; z++) {
            glm::vec3 chunkPos = glm::vec3(static_cast<int>(x * Chunk::chunkSize),
                -Chunk::baseTerrainHeight,
                static_cast<int>(z * Chunk::chunkSize));
            // Create a new chunk at (x, 0, z)
            chunks.emplace_back(chunkPos,seed);
            
            // Generate the mesh for the new chunk
            chunks.back().generateMesh();

            // Add a new model matrix for this chunk (initially an identity matrix)
            chunkModels.emplace_back(glm::translate(glm::mat4(1.0f),chunkPos));
            chunkCount++;
        }
    }

}

void Main::drawChunks() {
    for (int i = 0; i < chunkModels.size(); i++) {
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(chunkModels[i]));

        // Draw the chunk
        glBindVertexArray(chunks[i].VAO);

        for (const auto& pair : chunks[i].verticesByType) {
            BlockType type = pair.first;
            const auto& indices = chunks[i].indicesByType[type];

            if (indices.empty()) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureMap.at(type));
            glUniform1i(textureLocation, 0);

            glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT,
                (void*)(chunks[i].baseIndicesByType[type] * sizeof(unsigned int)));
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
        int chunkX = static_cast<int>(floor(currentPos.x / Chunk::chunkSize));
        int chunkZ = static_cast<int>(floor(currentPos.z / Chunk::chunkSize));

        // Find the chunk and compute local coordinates
        for (auto& chunk : chunks) {
            glm::vec3 chunkPos = chunk.chunkPosition;
            if (static_cast<int>(chunkPos.x / Chunk::chunkSize) == chunkX &&
                static_cast<int>(chunkPos.z / Chunk::chunkSize) == chunkZ) {
                // Adjust Y relative to chunk's base position
                int localX = static_cast<int>(floor(currentPos.x)) % Chunk::chunkSize;
                int localZ = static_cast<int>(floor(currentPos.z)) % Chunk::chunkSize;
                int localY = static_cast<int>(floor(currentPos.y - chunkPos.y)); // Offset by chunk Y

                if (localX < 0) localX += Chunk::chunkSize;
                if (localZ < 0) localZ += Chunk::chunkSize;

                if (localY >= 0 && localY < Chunk::chunkHeight) {
                    Block& block = chunk.blocks[localX][localY][localZ];
                    //when hit block
                    if (block.type != BlockType::AIR) {
                        highlightedBlockPos = glm::vec3(floor(currentPos.x), floor(currentPos.y), floor(currentPos.z));
                        hasHighlightedBlock = true;
                        
                        // Compute normal based on ray direction and hit position
                        glm::vec3 prevPos = rayOrigin + rayDir * (t - step);
                        prevBlock = glm::vec3(floor(prevPos.x), floor(prevPos.y), floor(prevPos.z));
                        
                        return;
                    }
                }
            }
        }
    }
}

void Main::placeBlock() {
    if (hasHighlightedBlock) {
        glm::vec3 placePos = prevBlock; // Place block on the face indicated by the normal
        int chunkX = static_cast<int>(floor(placePos.x / Chunk::chunkSize));
        int chunkZ = static_cast<int>(floor(placePos.z / Chunk::chunkSize));

        for (auto& chunk : chunks) {
            glm::vec3 chunkPos = chunk.chunkPosition;
            int chunkPosX = static_cast<int>(chunkPos.x / Chunk::chunkSize);
            int chunkPosZ = static_cast<int>(chunkPos.z / Chunk::chunkSize);

            if (chunkPosX == chunkX && chunkPosZ == chunkZ) {
                // Compute local coordinates relative to the chunk's position
                int localX = static_cast<int>(placePos.x - chunkPos.x);
                int localY = static_cast<int>(placePos.y - chunkPos.y);
                int localZ = static_cast<int>(placePos.z - chunkPos.z);

                // Ensure local coordinates are within chunk bounds
                if (localX >= 0 && localX < Chunk::chunkSize &&
                    localY >= 0 && localY < Chunk::chunkHeight &&
                    localZ >= 0 && localZ < Chunk::chunkSize) {
                    if (chunk.blocks[localX][localY][localZ].type == BlockType::AIR) {
                        chunk.setBlock(localX, localY, localZ, BlockType::STONE);
                        ///chunk.generateMesh(); // Regenerate mesh after placing
                    }
                    return;
                }
            }
        }
    }
}

void Main::breakBlock() { 
    if (hasHighlightedBlock) {
        int chunkX = static_cast<int>(floor(highlightedBlockPos.x / Chunk::chunkSize));
        int chunkZ = static_cast<int>(floor(highlightedBlockPos.z / Chunk::chunkSize));

        for (auto& chunk : chunks) {
            glm::vec3 chunkPos = chunk.chunkPosition;
            if (static_cast<int>(chunkPos.x / Chunk::chunkSize) == chunkX &&
                static_cast<int>(chunkPos.z / Chunk::chunkSize) == chunkZ) {
                int localX = static_cast<int>(highlightedBlockPos.x) % Chunk::chunkSize;
                int localY = static_cast<int>(highlightedBlockPos.y - chunkPos.y);
                int localZ = static_cast<int>(highlightedBlockPos.z) % Chunk::chunkSize;

                if (localX < 0) localX += Chunk::chunkSize;
                if (localZ < 0) localZ += Chunk::chunkSize;

                if (localY >= 0 && localY < Chunk::chunkHeight) {
                    chunk.setBlock(localX, localY, localZ, BlockType::AIR);
                   // chunk.generateMesh(); // Regenerate mesh after breaking
                    return;
                }
            }
        }
    }
}


void Main::run() {

    createCube();
    createLight();

    getTextures(); 
    addChunks(); 


    createShaders();
    createHighlight();


    player->spawn(glm::vec3(0,100,0));
    lastFrame = glfwGetTime();

    // Main loop
    while (!glfwWindowShouldClose(window)) {

        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        //input
        player->update(deltaTime,chunks);
        processInput(window);
        

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