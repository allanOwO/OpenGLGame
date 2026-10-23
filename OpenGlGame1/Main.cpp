#include "Main.h"
#include <iostream>
#include <random>
#include <string>

static const float PHYSICS_STEP = 1.0f / 120.0f; // seconds per physics update

Main::Main() {
    init();

    std::random_device rd;
    int seed = static_cast<int>(rd());
    std::cout << "Seed: " << seed << std::endl;

    world = std::make_unique<World>(seed);
    renderer = std::make_unique<Renderer>();
    player = std::make_unique<Player>(window);
}

Main::~Main() {
    // Order matters: World and Renderer delete GL objects, so they go before the window/context
    player.reset();
    world.reset();//waits for worker threads, then frees chunk buffers
    renderer.reset();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void Main::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    //update viewport
    glViewport(0, 0, width, height);

    // Update the width and height in the Main class
    Main* mainInstance = static_cast<Main*>(glfwGetWindowUserPointer(window));
    if (mainInstance) {
        mainInstance->width = width;
        mainInstance->height = height;
    }
}

void Main::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    Main* mainInstance = static_cast<Main*>(glfwGetWindowUserPointer(window));
    if (mainInstance && mainInstance->player) {
        mainInstance->player->processMouseMovement(xpos, ypos);
    }
}

void Main::init() {

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        exit(-1);
    }

    // Set OpenGL version to 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);


    // Create a window
    window = glfwCreateWindow(width, height, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        exit(-1);
    }


    // Make the OpenGL context current
    glfwMakeContextCurrent(window);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD!" << std::endl;
        exit(-1);
    }

    // Callbacks, the user pointer lets the static callbacks find this Main
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    //some opengl settings
    glEnable(GL_CULL_FACE);   // Enable face culling
    glCullFace(GL_BACK);      // Cull back faces (only render front faces)
    glFrontFace(GL_CCW);      // Define front faces as counterclockwise (CCW)
    glEnable(GL_DEPTH_TEST);
    glfwSwapInterval(0);// 1 = V-Sync on, 0 = V-Sync off
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);//wireframe mode
}

void Main::processInput()
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)//check for esc pressed
        glfwSetWindowShouldClose(window, true);

    //building
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        static double lastBreakTime = 0.0;
        double currentTime = glfwGetTime();
        if (currentTime - lastBreakTime > 0.2) { // 0.2s cooldown
            breakBlock();
            lastBreakTime = currentTime;
        }
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        static double lastPlaceTime = 0.0;
        double currentTime = glfwGetTime();
        if (currentTime - lastPlaceTime > 0.2) { // 0.2s cooldown
            placeBlock();
            lastPlaceTime = currentTime;
        }
    }
}

void Main::placeBlock() {
    if (!hasTarget) return;

    // Empty block next to the hit face
    if (player->blockIntersects(glm::vec3(target.previous))) {
        return; // Cancel placement if a player is in the way
    }
    world->setBlock(target.previous, BlockType::STONE);
}

void Main::breakBlock() {
    if (!hasTarget) return;
    world->setBlock(target.block, BlockType::AIR);
}

void Main::doFps() {
    float currentTime = glfwGetTime();
    frameCount++;

    // Update FPS every second
    if (currentTime - lastFPSTime >= 1.0f) {
        fps = frameCount / (currentTime - lastFPSTime);
        frameCount = 0;
        lastFPSTime = currentTime;

        glm::ivec3 position = player->getCameraPos();

        // Update window title with FPS
        std::string title = "My Game - FPS: " + std::to_string(fps)
            + " Position: "
            + "X:" + std::to_string(position.x)
            + " Y:" + std::to_string(position.y)
            + " Z:" + std::to_string(position.z);

        glfwSetWindowTitle(window, title.c_str());
    }
}

void Main::run() {

    player->spawn(glm::vec3(10, 200, 10));
    lastFrame = glfwGetTime();

    // Main loop
    while (!glfwWindowShouldClose(window)) {

        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Cap deltaTime so a long stall (window drag, breakpoint) doesn't run hundreds of physics steps
        const float maxDeltaTime = 0.25f;
        if (deltaTime > maxDeltaTime) {
            deltaTime = maxDeltaTime;
        }

        // Physics runs in fixed steps so movement is the same at any frame rate
        physicsAccumulator += deltaTime;
        while (physicsAccumulator >= PHYSICS_STEP) {
            player->update(PHYSICS_STEP, *world);
            physicsAccumulator -= PHYSICS_STEP;
        }
        // Camera blends between the last two steps so it stays smooth above the physics rate
        player->interpolate(physicsAccumulator / PHYSICS_STEP);

        // What the player is looking at, used for building and the outline
        hasTarget = world->raycast(player->getCameraPos(), player->getCameraFront(), reachDistance, target);
        processInput();

        world->update(player->getCameraPos());

        renderer->render(*world, player->getCameraPos(), player->getViewMatrix(),
            width, height, currentFrame, hasTarget ? &target : nullptr);
        doFps();

#ifdef _DEBUG
        // One check per frame, glGetError after every call stalls the GPU
        for (GLenum err; (err = glGetError()) != GL_NO_ERROR;) {
            std::cerr << "OpenGL error this frame: 0x" << std::hex << err << std::dec << std::endl;
        }
#endif

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

int main()
{
    Main main;

    main.run();

    return 0;
}
