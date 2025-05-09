#pragma once
#include "Model.h"
#include <iostream>
#include "ModelLoader.h"



Model::~Model() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteTextures(1, &textureID);
}

void Model::draw(unsigned int shaderProgram) {
    // Early exit on bad data
    if (vertices.empty() || indices.empty()) {
        std::cerr << "Cannot draw model: empty vertices or indices\n";
        return;
    }
    if (VAO == 0 || VBO == 0 || EBO == 0) {
        std::cerr << "Cannot draw model: invalid VAO/VBO/EBO\n";
        return;
    }

    if (!glIsVertexArray(VAO)) {
        std::cerr << "Invalid VAO: " << VAO << std::endl;
        return;
    }
    if (!glIsProgram(shaderProgram)) {
        std::cerr << "Invalid shader program: " << shaderProgram << std::endl;
        return;
    } 


    // 1) Activate shader
    glUseProgram(shaderProgram);
    checkGLError("glUseProgram"); 

    // 2) Set texture sampler uniform (if your frag shader samples it)
    GLint texLoc = glGetUniformLocation(shaderProgram, "texture0");
    if (texLoc >= 0) {
        glUniform1i(texLoc, 0);      // texture0 -> texture unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        checkGLError("glBindTexture"); 
    }

    // 3) Bind VAO and ensure EBO is bound
    glBindVertexArray(VAO);
    checkGLError("glBindVertexArray");
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO); 
    checkGLError("glBindBuffer EBO");

    // 5) Error check
    GLenum err; 
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL error before glDrawElements: 0x"
            << std::hex << err << std::dec << "\n";
    } 
 

    // 4) Draw elements
    glDrawElements(GL_TRIANGLES,
        static_cast<GLsizei>(indices.size()),
        GL_UNSIGNED_INT,
        0); 

    // 5) Error check
    
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL error after glDrawElements: 0x"
            << std::hex << err << std::dec << "\n";
    }

    // Optional: clean up binding
    glBindVertexArray(0);
    if (texLoc >= 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Model::checkGLError(const std::string& stage) { 
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL error at " << stage << ": 0x" << std::hex << err << std::dec << std::endl;
    }
}
