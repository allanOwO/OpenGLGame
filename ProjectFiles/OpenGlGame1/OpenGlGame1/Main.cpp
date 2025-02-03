#include <iostream>
#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include "Main.h"
#define STB_IMAGE_IMPLEMENTATION  // This tells stb to include the implementation
#include "stb/stb_image.h"          // Path to the stb_image.h file


Main::Main() : window(nullptr){

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

void framebuffer_size_callback(GLFWwindow* window, int width, int height);//define function(doesnt work inn main because it needs to be a free function

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
    window = glfwCreateWindow(800, 600, "OpenGL Window", NULL, NULL);
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


    glViewport(0, 0, 800, 600);



    // Set framebuffer size callback (this was missing)
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
}

void Main::processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)//check for esc pressed
        glfwSetWindowShouldClose(window, true);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void Main::createShaders() {

    shader = new Shader("vertex_shader.glsl", "fragment_shader.glsl");
}

void Main::createBufferObjects() {

    float vertices[] = {
    //positions          //colours        //texture coords
     0.5f,  0.5f, 0.0f,  1.0f,0.0f,0.0f,  1.0f,1.0f,// top right
     0.5f, -0.5f, 0.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,// bottom right
    -0.5f, -0.5f, 0.0f,  0.0f,0.0f,1.0f,  0.0f,0.0f,// bottom left
    -0.5f,  0.5f, 0.0f,  1.0f,1.0f,0.0f,  0.0f,1.0f// top left 
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

}

void Main::render() {

    // set background & clear screen
    glClearColor(0.1, 0.4, 0.6, 1);
    glClear(GL_COLOR_BUFFER_BIT); 

    shader->use();

    glActiveTexture(GL_TEXTURE0); 
    glBindTexture(GL_TEXTURE_2D, texture); 
    

    // update the uniform color
    float timeValue = glfwGetTime();
    float greenValue = sin(timeValue) / 2.0f + 0.5f;
    
    shader->setVec4("ourColour",glm::vec4(0, greenValue,0,1.0f));
    shader->setInt("ourTexture", 0);

    
    // The first argument is the drawing mode (like glDrawArrays). The second is the number of elements to draw (6 vertices). 
    // The third is the index type (GL_UNSIGNED_INT). The last is the offset (set to 0 here).
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO); 
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0); 

    //v to draw a triangle from array
    //glBindVertexArray(VAO);
    //glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Main::run() {

    createBufferObjects();

    createShaders();

    //texture code ---------------
    int width, height, nrChannels;
    unsigned char* data = stbi_load("../ResourceFiles/container.jpg", &width, &height, &nrChannels, 0);

    if (!data) {
        std::cerr << "Failed to load texture!" << std::endl;
        exit(-1); // Exit if the texture couldn't be loaded
    }

    glGenTextures(1, &texture); 

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);//free memory
    //texture code ---------------


    // Main loop
    while (!glfwWindowShouldClose(window)) {
        //input
        processInput(window);

        render();

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





