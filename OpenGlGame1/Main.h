#pragma once

#include <glad/glad.h>//must go before glfw
#include <GLFW/glfw3.h>
#include <memory>

#include "World.h"
#include "Renderer.h"
#include "Player.h"
#include "UIRenderer.h"
#include "UI.h"
#include "Menu.h"
#include "Settings.h"
#include "WorldSave.h"
#include "DebugOverlay.h"
#include "Benchmark.h"

// The application: owns the window, the game loop and input, and ties World, Player, Renderer and the menus together
// States: no world (title menus) -> playing -> paused (world still drawn, menu on top)
class Main
{
public:
	Main();
	~Main();

	void run();
	void runBenchmarkAndQuit();//for the --benchmark command line flag, call before run()

private:
	void init(); // Initialize GLFW, GLAD, etc.

	// game
	void updateGame(float deltaTime);
	void processGameInput();
	void placeBlock(); // Place a block next to the targeted one
	void breakBlock(); // Break the targeted block

	// worlds
	void startWorld(const WorldInfo& info);
	bool saveWorld();
	void unloadWorld();

	// benchmark, runs in its own throwaway world that is never saved
	void startBenchmark();
	void finishBenchmark();

	// menus
	void handleMenuAction(const MenuAction& action);
	void handleEscape();
	void updateCursorMode();
	void drawHud(const UIInput& input);//crosshair, debug overlay, benchmark progress

	void doFps();

	// GLFW callbacks are plain functions, the window user pointer gets them back to this Main
	static Main* fromWindow(GLFWwindow* window);
	static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	static void charCallback(GLFWwindow* window, unsigned int codepoint);
	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

	GLFWwindow* window = nullptr; // Window pointer
	int width = 1920, height = 1080;

	// Created after the GL context exists, destroyed before it goes (see ~Main)
	std::unique_ptr<Renderer> renderer;
	std::unique_ptr<UIRenderer> uiRenderer;
	std::unique_ptr<UI> ui;

	// Only exist while a world is open
	std::unique_ptr<World> world;
	std::unique_ptr<Player> player;
	WorldInfo currentWorld;
	std::unique_ptr<Benchmark> benchmark;//while it exists the benchmark drives the camera instead of the player
	bool quitAfterBenchmark = false;

	DebugOverlay debugOverlay;
	FrameTimes lastFrameTimes;//measured during the previous frame, recorded at the start of the next one

	Menu menu;
	Settings settings;
	bool settingsDirty = false;//changed since last written to settings.txt

	// Input gathered by the callbacks between frames, handed to the UI once per frame
	UIInput pendingInput;
	glm::vec2 mousePosition = glm::vec2(0.0f);
	bool escapePressed = false;
	bool cursorCaptured = false;
	bool ignoreMouseUntilRelease = false;//stops the click that closes a menu from also breaking a block

	//building
	float reachDistance = 5.0f; // Max distance player can build
	bool hasTarget = false; // True if a block is in range
	RaycastHit target; // Block the player is looking at

	//fps tracking & timing
	float lastFPSTime = 0.0f; // Time of the last FPS update
	int frameCount = 0;        // Number of frames since last update
	float fps = 0.0f;          // Current FPS value
	float lastFrame = 0.0f; // Time of last frame
	double lastFrameStart = 0.0;//nowSeconds(), double so frame times stay precise however long the game runs
	float physicsAccumulator = 0.0f; // Frame time not yet used by a physics step
};
