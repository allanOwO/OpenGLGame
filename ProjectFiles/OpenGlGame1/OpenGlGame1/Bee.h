#pragma once
#include "Mob.h"
#include <string>

class Bee : public Mob
{
public:
	Bee(const glm::vec3& position);
	void update(float deltaTime) override;

private:
	static const std::string modelPath;

	//add movement later

};

