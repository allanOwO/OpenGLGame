#pragma once
#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h> // include glad to get all the required OpenGL headers
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <glm/glm.hpp>

class Shader
{
public:
    unsigned int ID; // Shader program ID

    // Constructor that reads and builds the shader
    Shader(const char* vertexPath, const char* fragmentPath);

    // Activate the shader program
    void use();

    // Utility functions to set uniforms
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec4(const std::string& name, glm::vec4 value) const;
    void setVec3(const std::string& name, glm::vec3 value) const;
        
};

#endif
