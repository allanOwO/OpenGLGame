#pragma once

#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include <memory>

#include "World.h"
#include "Renderer.h"
#include "Player.h"

// The application: owns the window, the game loop and input, and ties World, Player and Renderer together
class Main
{
public:
	Main();
	~Main();

	void run();

private:
	void init(); // Initialize GLFW, GLAD, etc.
	void processInput();
	void placeBlock(); // Place a block next to the targeted one
	void breakBlock(); // Break the targeted block
	void doFps();

	// GLFW callbacks are plain functions, the window user pointer gets them back to this Main
	static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);

	GLFWwindow* window = nullptr; // Window pointer
	int width = 1920, height = 1080;

	// Created after the GL context exists, destroyed before it goes (see ~Main)
	std::unique_ptr<World> world;
	std::unique_ptr<Renderer> renderer;
	std::unique_ptr<Player> player;

	//building
	float reachDistance = 5.0f; // Max distance player can build
	bool hasTarget = false; // True if a block is in range
	RaycastHit target; // Block the player is looking at

	//fps tracking & timing
	float lastFPSTime = 0.0f; // Time of the last FPS update
	int frameCount = 0;        // Number of frames since last update
	float fps = 0.0f;          // Current FPS value
	float lastFrame = 0.0f; // Time of last frame
	float physicsAccumulator = 0.0f; // Frame time not yet used by a physics step
};
