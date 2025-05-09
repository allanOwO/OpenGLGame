#include "Mob.h"
#include <iostream>

Mob::Mob(Model* model,const glm::vec3 position)
	:model(model), position(position), yaw(0.0){}

void Mob::ChangeHealth(float amount) {
	health += amount;
	if (health > maxHealth) health = maxHealth;

	else if (health <= 0)
		std::cout << "dead" << std::endl;
		//die
}

void Mob::update(float dt) {

	//move, turn ect
}

void Mob::render(unsigned int shaderProgram) {
    if (!model) {
        std::cerr << "No model to render!" << std::endl;
        return;
    }

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, position);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(4, 4, 4));

    GLint loc = glGetUniformLocation(shaderProgram, "model");
    if (loc == -1) {
        std::cerr << "Model uniform not found in shader" << std::endl;
    }
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(modelMatrix));
    if (GLenum err = glGetError()) std::cerr << "Error after glUniformMatrix4fv: 0x" << std::hex << err << std::dec << std::endl;

    glDisable(GL_CULL_FACE);
    if (GLenum err = glGetError()) std::cerr << "Error after glDisable(GL_CULL_FACE): 0x" << std::hex << err << std::dec << std::endl;

    glDepthFunc(GL_LEQUAL);
    if (GLenum err = glGetError()) std::cerr << "Error after glDepthFunc(GL_LEQUAL): 0x" << std::hex << err << std::dec << std::endl;

    model->draw(shaderProgram);

    glEnable(GL_CULL_FACE);
    if (GLenum err = glGetError()) std::cerr << "Error after glEnable(GL_CULL_FACE): 0x" << std::hex << err << std::dec << std::endl;

    glDepthFunc(GL_LESS);
    if (GLenum err = glGetError()) std::cerr << "Error after glDepthFunc(GL_LESS): 0x" << std::hex << err << std::dec << std::endl;

    glBindVertexArray(0);
    if (GLenum err = glGetError()) std::cerr << "Error after glBindVertexArray: 0x" << std::hex << err << std::dec << std::endl;
}
