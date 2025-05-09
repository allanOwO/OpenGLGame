#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <iostream>

struct Vertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
	glm::vec3 normal;
};

class Model {

public:
	unsigned int VAO, VBO, EBO;
	unsigned int textureID;
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;

	Model(): VAO(0), VBO(0), EBO(0), textureID(0){}
	~Model();
	void draw(unsigned int shaderProgram);

private:
	void checkGLError(const std::string& stage);
};