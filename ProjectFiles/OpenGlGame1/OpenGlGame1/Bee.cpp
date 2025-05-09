#include "Bee.h"
#include "ModelLoader.h"
#include <iostream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

const std::string Bee::modelPath = "../ResourceFiles/bee.glb";

Bee::Bee(const glm::vec3& position)
	: Mob(ModelLoader::getModel(modelPath), position){

    if (!model) {
        std::cerr << "Failed to load bee model!" << std::endl; 
    }
    else if (model->VAO == 0) {
        std::cerr << "Bee model has invalid VAO!" << std::endl;
    }

    std::cout << "Bee created at position: " << glm::to_string(position) << std::endl;

}

void Bee::update(float deltaTime) {

	std::cout << "bee x " << position.x << " bee y " << position.y << " bee z " << position.z << std::endl;
}

