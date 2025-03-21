#include <iostream>
#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include "Main.h"
#define STB_IMAGE_IMPLEMENTATION  // This tells stb to include the implementation
#include "stb/stb_image.h"          // Path to the stb_image.h file

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>



Main::Main() : window(nullptr),width(1280),height(720){

    init();
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

    //set the pointer
    glfwSetWindowUserPointer(window, this);

    // Set framebuffer size callback  
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    //lock mouse to scrreen
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); 

    //using a lambda
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        // Retrieve the instance pointer from the user pointer.
        Main* mainInstance = static_cast<Main*>(glfwGetWindowUserPointer(window));
        if (mainInstance) {
            mainInstance->mouse_callback(window, xpos, ypos);
        } 
    }); 
}

void Main::processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)//check for esc pressed
        glfwSetWindowShouldClose(window, true);


    //cam movement
    float currentFrame = glfwGetTime();
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    float camSpeed = camSpeedBase * deltaTime;
    
    //fw & bw move
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += camSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= camSpeed * cameraFront;
    //L  & R move
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;
}

//handles looking
void Main::mouse_callback(GLFWwindow* window, double xpos, double ypos) {

    if (firstMouse)//stops large 1st jump
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    //calculate difference since last frame
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates range from bottom to top
    lastX = xpos;
    lastY = ypos;

    const float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    //add offset to stored rotation values
    yaw += xoffset;
    pitch += yoffset;

    //vertical clamping
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    //do the movement part
    lookDirection.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    lookDirection.y = sin(glm::radians(pitch));
    lookDirection.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(lookDirection);//set cam front to new direcction
}



void Main::createShaders() {

    shader = new Shader("vertex_shader.glsl", "fragment_shader.glsl");
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
    unsigned int indices[] = {  // note that we start from 0!
        0, 1, 3,   // first triangle
        1, 2, 3    // second triangle
    };

    //create element buffer object, used for more complex shapes, eg square
    glGenBuffers(1, &EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);


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


    //unblund vao
    glBindVertexArray(0);

}

void Main::render() {

    // set background & clear screen
    glClearColor(0.1, 0.4, 0.6, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 
    

    shader->use();

    //vie and projection matrices
    //lookat(position, target, up)
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), (float)width / (float)height, 0.1f, 100.0f);

    //pass view and projection matrices to shader
    glUniformMatrix4fv(glGetUniformLocation(shader->ID, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader->ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    
    //model matrice for rotating cube
    glm::mat4 cubeModel = glm::mat4(1.0f);
    // Rotate the object
    cubeModel = glm::rotate(cubeModel, (float)glfwGetTime() * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f)); 

    glUniformMatrix4fv(glGetUniformLocation(shader->ID, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel)); 

    //draw the rotating cube
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    
    drawChunks();
}

void Main::addChunks() {

    
    for (int x = 0; x <10; x++) {
        for (int z = 0; z < 10; z++) {
            glm::vec3 chunkPos = glm::vec3(static_cast<int>(x * Chunk::chunkSize),
                0,
                static_cast<int>(z * Chunk::chunkSize));
            // Create a new chunk at (x, 0, z)
            chunks.emplace_back(chunkPos);
            
            // Generate the mesh for the new chunk
            chunks.back().generateMesh();

            // Add a new model matrix for this chunk (initially an identity matrix)
            chunkModels.emplace_back(glm::translate(glm::mat4(1.0f),chunkPos));
        }
    }
    
}

void Main::drawChunks() {

    for (int i = 0;i < chunkModels.size();i++) {

        glUniformMatrix4fv(glGetUniformLocation(shader->ID, "model"), 1, GL_FALSE, glm::value_ptr(chunkModels[i]));

        //draw the chunk
        glBindVertexArray(chunks[i].VAO);


        for (const auto& pair : chunks[i].verticesByType) {
            BlockType type = pair.first;
            const auto& indices = chunks[i].indicesByType[type];

            if (indices.empty()) continue;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureMap.at(type));
            glUniform1i(glGetUniformLocation(shader->ID, "texture1"), 0);

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

void Main::run() {

    createCube();

    getTextures(); 

    addChunks();

    
    createShaders();


    glEnable(GL_CULL_FACE);   // Enable face culling 
    glCullFace(GL_BACK);      // Cull back faces (only render front faces) 
    glFrontFace(GL_CCW);      // Define front faces as counterclockwise (CCW) 
    glEnable(GL_DEPTH_TEST); 

    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);//wireframe mode

     
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        //input
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





