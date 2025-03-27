#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "Chunk.h"

#include<unordered_map>
#include "Vec3Hash.h"

#define GRAVITY -21.0f//as a downwards force
#define JUMP_HEIGHT 1.2f
#define SPRINT_SPEED 1.3f
#define BASE_SPEED 8.0f

//players bounding box(collsisions)
struct AABB {
	glm::vec3 min;
	glm::vec3 max;

	AABB(const glm::vec3& min, const glm::vec3& max) : min(min), max(max) {} 
};
 
class Player
{
public:

	Player(GLFWwindow* window);
	
	void spawn(glm::vec3 spawnPos); 
	void update(float deltaTime, const std::unordered_map<glm::vec3, Chunk, Vec3Hash>& chunks); 

	// Getters for rendering
	glm::vec3 getCameraPos() const { return cameraPos; }
	glm::vec3 getCameraFront() const { return cameraFront; }
	glm::vec3 getCameraUp() const { return cameraUp; }
	glm::mat4 getViewMatrix() const; 

	bool blockIntersects(const glm::vec3& blockPos) const;
	

private:
	void processMouseMovement(GLFWwindow* window, double xpos, double ypos);
	void playerMovement(float deltaTime, const std::unordered_map<glm::vec3, Chunk, Vec3Hash>& chunks);

	GLFWwindow* window;	

	// camera
	bool firstMouse;//used to ignore the mouses 1st frame to stop a large jump
	glm::vec3 cameraPos = glm::vec3(0.0f, 120.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

	glm::vec3 lookDirection;
	float yaw = -90.0f;//offest at beginign to allign to -z
	float pitch = 0.0f;

	const float lookSens = 0.1f;

	//mouse movement
	float lastX = 400, lastY = 300;

	//movement
	glm::vec3 bodyPos;
	float eyeLevel = 1.6f;
	glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f); // Player velocity
	
	bool intersects(const AABB& a, const AABB& b) const;
	AABB playerAABB = { glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 1.8f, 0.3f) }; // Example size 
	bool grounded;

};

