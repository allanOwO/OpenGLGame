#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Model.h"
#include <string>

class Mob
{
public:

	
	glm::vec3 position;
	float maxHealth;//assume player has 100hp as baseline
	float health;
	bool passive;//aggressive or not
	Model* model;
	 
	Mob(Model* model, const glm::vec3 position);
	virtual ~Mob() {};

	void spawn();
	virtual void update(float deltaTime) = 0;
	virtual void render(unsigned int shaderProgram);
	void ChangeHealth(float amount);

private:
	
	float yaw;
	
};

