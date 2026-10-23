#pragma once
#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class World;

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

	explicit Player(GLFWwindow* window);//window is only used to read keys

	void spawn(glm::vec3 spawnPos);
	void update(float deltaTime, const World& world);//one physics step
	void interpolate(float alpha);//0-1 between the previous and current physics step, sets the camera position

	// Getters for rendering
	glm::vec3 getCameraPos() const { return cameraPos; }
	glm::vec3 getCameraFront() const { return cameraFront; }
	glm::vec3 getCameraUp() const { return cameraUp; }
	glm::mat4 getViewMatrix() const;

	bool blockIntersects(const glm::vec3& blockPos) const;
	void processMouseMovement(double xpos, double ypos);
	void resetMouse() { firstMouse = true; }//next mouse move is a fresh start, call when the cursor comes back from a menu

	// For saving and loading
	glm::vec3 getPosition() const { return bodyPos; }//feet
	float getYaw() const { return yaw; }
	float getPitch() const { return pitch; }
	void setLook(float newYaw, float newPitch);

private:
	void playerMovement(float deltaTime, const World& world);
	void updateCameraFront();//from yaw and pitch
	void moveAxis(int axis, float amount, glm::vec3& position, const World& world);

	GLFWwindow* window;

	// camera
	bool firstMouse = true;//used to ignore the mouses 1st frame to stop a large jump
	glm::vec3 cameraPos = glm::vec3(0.0f, 120.0f, 3.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

	float yaw = -90.0f;//offest at beginign to allign to -z
	float pitch = 0.0f;

	const float lookSens = 0.1f;

	//mouse movement
	float lastX = 400, lastY = 300;

	//movement
	glm::vec3 bodyPos = glm::vec3(0.0f);
	glm::vec3 previousBodyPos = glm::vec3(0.0f);//body position at the previous physics step
	float eyeLevel = 1.6f;
	glm::vec3 velocity = glm::vec3(0.0f, 0.0f, 0.0f); // Player velocity

	bool intersects(const AABB& a, const AABB& b) const;
	AABB playerAABB = { glm::vec3(-0.3f, 0.0f, -0.3f), glm::vec3(0.3f, 1.8f, 0.3f) }; // Example size
	bool grounded = false;
};

