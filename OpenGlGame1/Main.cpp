#include "Main.h"
#include "GLFunctions.h"
#include "Screenshot.h"
#include <iostream>
#include <random>
#include <string>
#include <cmath>

static const float PHYSICS_STEP = 1.0f / 120.0f; // seconds per physics update
static const char* SETTINGS_FILE = "settings.txt";

Main::Main() {
    init();
    settings.load(SETTINGS_FILE);//keeps the defaults if there's no file yet

    renderer = std::make_unique<Renderer>();
    uiRenderer = std::make_unique<UIRenderer>();
    ui = std::make_unique<UI>(*uiRenderer);
    menu.open(MenuScreen::Title);
}

Main::~Main() {
    // Closing the window while playing shouldn't lose anything
    if (world && !benchmark) saveWorld();
    if (settingsDirty) settings.save(SETTINGS_FILE);

    // Order matters: these delete GL objects, so they go before the window/context
    unloadWorld();//waits for worker threads, then frees chunk buffers
    ui.reset();
    uiRenderer.reset();
    renderer.reset();

    glfwDestroyWindow(window);
    glfwTerminate();
}

// ---------- window and input callbacks ----------

Main* Main::fromWindow(GLFWwindow* window) {
    return static_cast<Main*>(glfwGetWindowUserPointer(window));
}

void Main::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    //update viewport
    glViewport(0, 0, width, height);

    // Update the width and height in the Main class
    if (Main* main = fromWindow(window)) {
        main->width = width;
        main->height = height;
    }
}

void Main::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    Main* main = fromWindow(window);
    if (!main) return;
    main->mousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    // Only turn the camera while actually playing, in menus the mouse moves the cursor
    if (main->player && main->cursorCaptured) {
        main->player->processMouseMovement(xpos, ypos);
    }
}

void Main::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    // A click can be shorter than a frame, so remember it here instead of polling for it
    Main* main = fromWindow(window);
    if (main && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        main->pendingInput.mousePressed = true;
    }
}

void Main::charCallback(GLFWwindow* window, unsigned int codepoint)
{
    // Typed text (respects shift, keyboard layout etc), only ASCII since that's what the font has
    Main* main = fromWindow(window);
    if (main && codepoint < 128) {
        main->pendingInput.typed.push_back(static_cast<char>(codepoint));
    }
}

void Main::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Main* main = fromWindow(window);
    if (!main) return;
    // Repeat lets holding backspace delete several characters
    if (key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        main->pendingInput.backspaces++;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        main->escapePressed = true;
    }
    if (key == GLFW_KEY_F3 && action == GLFW_PRESS) {
        main->debugOverlay.toggle();
    }
}

void Main::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (Main* main = fromWindow(window)) {
        main->pendingInput.scroll += static_cast<float>(yoffset);
    }
}

void Main::init() {

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        exit(-1);
    }

    // OpenGL 4.3 Core Profile, 4.3 is the first version with glMultiDrawElementsIndirect (all chunks in one draw call)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);


    // Create a window
    window = glfwCreateWindow(width, height, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window! (needs a GPU and driver with OpenGL 4.3)" << std::endl;
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
    if (!GLFunctions::load((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Driver is missing OpenGL 4.3 functions!" << std::endl;
        exit(-1);
    }

    // The window may not be the size we asked for (e.g. bigger than the screen)
    glfwGetFramebufferSize(window, &width, &height);

    // Callbacks, the user pointer lets the static callbacks find this Main
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetScrollCallback(window, scrollCallback);
    // Starts on the title screen, so the cursor is visible (captured when a world starts, see updateCursorMode)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    //some opengl settings
    glEnable(GL_CULL_FACE);   // Enable face culling
    glCullFace(GL_BACK);      // Cull back faces (only render front faces)
    glFrontFace(GL_CCW);      // Define front faces as counterclockwise (CCW)
    glEnable(GL_DEPTH_TEST);
    glfwSwapInterval(0);// 1 = V-Sync on, 0 = V-Sync off
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);//wireframe mode
}

// ---------- playing ----------

void Main::updateGame(float deltaTime) {
    // Until the chunk under the player exists there's nothing to stand on, so hold physics
    // (otherwise a loaded save can drop the player through the ground before it generates)
    if (!world->isLoadedAt(player->getPosition())) {
        physicsAccumulator = 0.0f;
        return;
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
    processGameInput();
}

void Main::processGameInput()
{
    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    // The click on "RESUME" is still held when the game starts again, wait for it to be let go
    if (ignoreMouseUntilRelease) {
        if (leftDown || rightDown) return;
        ignoreMouseUntilRelease = false;
    }

    //building
    if (leftDown) {
        static double lastBreakTime = 0.0;
        double currentTime = glfwGetTime();
        if (currentTime - lastBreakTime > 0.2) { // 0.2s cooldown
            breakBlock();
            lastBreakTime = currentTime;
        }
    }
    if (rightDown) {
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

// ---------- worlds ----------

void Main::startWorld(const WorldInfo& info) {
    unloadWorld();
    currentWorld = info;
    std::cout << "Opening world '" << info.name << "', seed " << info.seed << std::endl;

    world = std::make_unique<World>(info.seed, settings.renderDistance);
    // Edited chunks come from the save, everything else regenerates from the seed (missing file = new world)
    world->loadChunks(WorldSave::chunksPath(info.name));

    player = std::make_unique<Player>(window);
    if (info.hasPlayer) {
        player->spawn(info.playerPosition);
        player->setLook(info.yaw, info.pitch);
    }
    else {
        // New world: stand on top of the terrain at the origin
        // getHeight is in chunk local y, chunks sit baseTerrainHeight below world y = 0
        int surface = static_cast<int>(world->getTerrain().getHeight(0.0f, 0.0f)) - Chunk::baseTerrainHeight;
        player->spawn(glm::vec3(0.5f, surface + 1.0f, 0.5f));
    }

    physicsAccumulator = 0.0f;
    hasTarget = false;
    menu.close();
}

bool Main::saveWorld() {
    if (!world) return false;

    currentWorld.hasPlayer = true;
    currentWorld.playerPosition = player->getPosition();
    currentWorld.yaw = player->getYaw();
    currentWorld.pitch = player->getPitch();

    // Chunks first: world.txt is what makes the folder show up as a world, so it's written last
    bool chunksSaved = world->saveChunks(WorldSave::chunksPath(currentWorld.name));
    bool infoSaved = WorldSave::writeInfo(currentWorld);
    if (!chunksSaved || !infoSaved) {
        std::cerr << "Saving world '" << currentWorld.name << "' failed" << std::endl;
    }
    return chunksSaved && infoSaved;
}

void Main::unloadWorld() {
    benchmark.reset();
    player.reset();
    world.reset();
    hasTarget = false;
}

// ---------- benchmark ----------

void Main::startBenchmark() {
    unloadWorld();
    std::cout << "Starting benchmark, render distance " << settings.renderDistance << std::endl;

    // A fresh world every run, nothing is loaded from or saved to disk so runs can't affect each other
    world = std::make_unique<World>(Benchmark::seed, settings.renderDistance);
    player = std::make_unique<Player>(window);//not moved, but the rest of Main expects one alongside a world
    benchmark = std::make_unique<Benchmark>(*world, width, height);

    hasTarget = false;
    menu.close();
}

void Main::finishBenchmark() {
    std::vector<std::string> results = benchmark->finish();
    unloadWorld();
    menu.showBenchmarkResults(results);
    if (quitAfterBenchmark) glfwSetWindowShouldClose(window, true);
}

void Main::runBenchmarkAndQuit() {
    quitAfterBenchmark = true;
    startBenchmark();
}

// ---------- menus ----------

void Main::handleMenuAction(const MenuAction& action) {
    using Type = MenuAction::Type;
    switch (action.type) {
    case Type::Quit:
        glfwSetWindowShouldClose(window, true);
        break;

    case Type::CreateWorld: {
        WorldInfo info;
        info.name = action.worldName;
        if (!Menu::parseSeed(action.seedText, info.seed)) {
            std::random_device rd;
            info.seed = static_cast<int>(rd());
        }
        // Write world.txt straight away so the world exists even if the game crashes before the first save
        if (!WorldSave::writeInfo(info)) {
            menu.showMessage("COULDN'T CREATE THE WORLD FOLDER");
            break;
        }
        startWorld(info);
        break;
    }

    case Type::LoadWorld: {
        WorldInfo info;
        if (!WorldSave::readInfo(action.worldName, info)) {
            menu.showMessage("COULDN'T READ THAT WORLD");
            break;
        }
        startWorld(info);
        break;
    }

    case Type::Resume:
        menu.close();
        break;

    case Type::SaveWorld:
        menu.showMessage(saveWorld() ? "GAME SAVED" : "SAVE FAILED");
        break;

    case Type::SaveAndExitToTitle:
        saveWorld();
        unloadWorld();
        menu.open(MenuScreen::Title);
        break;

    case Type::SaveAndQuit:
        saveWorld();
        unloadWorld();//already saved, so the destructor doesn't save again
        glfwSetWindowShouldClose(window, true);
        break;

    case Type::StartBenchmark:
        startBenchmark();
        break;

    case Type::SettingsChanged:
        if (world) world->setRenderDistance(settings.renderDistance);
        settingsDirty = true;
        break;

    default:
        break;
    }
}

void Main::handleEscape() {
    if (benchmark) {
        unloadWorld();
        menu.open(MenuScreen::Title);
        menu.showMessage("BENCHMARK CANCELLED");
    }
    else if (world && !menu.isOpen()) menu.open(MenuScreen::Pause);
    else menu.goBack();
}

void Main::updateCursorMode() {
    // Captured (hidden, mouse turns the camera) only while playing with no menu open
    bool wantCaptured = world && !menu.isOpen();
    if (wantCaptured == cursorCaptured) return;

    cursorCaptured = wantCaptured;
    glfwSetInputMode(window, GLFW_CURSOR, wantCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (wantCaptured) {
        player->resetMouse();//cursor moved while in the menu, don't turn that into a camera jump
        ignoreMouseUntilRelease = true;
    }
}

void Main::drawHud(const UIInput& input) {
    ui->beginFrame(input, width, height);
    float s = static_cast<float>(ui->scale());

    if (benchmark) {
        debugOverlay.draw(*ui, *world, *renderer, benchmark->cameraPos(), benchmark->cameraFront(), benchmark->getYaw(), benchmark->getPitch());
        // Progress along the bottom, out of the way of the overlay
        std::string status = benchmark->status();
        float textW = UIRenderer::textWidth(status, ui->scale());
        float textH = UIRenderer::textHeight(ui->scale());
        float x = std::round((width - textW) * 0.5f), y = height - textH - 8 * s;
        ui->panel(x - 3 * s, y - 3 * s, textW + 6 * s, textH + 6 * s, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
        ui->label(status, x, y, ui->scale(), glm::vec4(1.0f, 0.85f, 0.4f, 1.0f));
    }
    else {
        float cx = std::round(width * 0.5f), cy = std::round(height * 0.5f);
        glm::vec4 colour(1.0f, 1.0f, 1.0f, 0.8f);
        ui->panel(cx - 5 * s, cy - s * 0.5f, 10 * s, s, colour);
        ui->panel(cx - s * 0.5f, cy - 5 * s, s, 10 * s, colour);
        debugOverlay.draw(*ui, *world, *renderer, player->getCameraPos(), player->getCameraFront(), player->getYaw(), player->getPitch());
    }
    ui->endFrame();
}

void Main::doFps() {
    float currentTime = glfwGetTime();
    frameCount++;

    // Update FPS every second
    if (currentTime - lastFPSTime >= 1.0f) {
        fps = frameCount / (currentTime - lastFPSTime);
        frameCount = 0;
        lastFPSTime = currentTime;

        // Update window title with FPS
        std::string title = "My Game - FPS: " + std::to_string(fps);
        if (player) {
            glm::ivec3 position = player->getCameraPos();
            title += " Position: X:" + std::to_string(position.x)
                + " Y:" + std::to_string(position.y)
                + " Z:" + std::to_string(position.z);
        }

        glfwSetWindowTitle(window, title.c_str());
    }
}

void Main::run() {

    lastFrame = glfwGetTime();
    lastFrameStart = nowSeconds();

    // Main loop
    while (!glfwWindowShouldClose(window)) {

        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // The frame that just ended is complete now, hand its times to whoever is measuring
        double frameStart = nowSeconds();
        double frameSeconds = frameStart - lastFrameStart;
        lastFrameStart = frameStart;
        lastFrameTimes.frameMs = static_cast<float>(frameSeconds * 1000.0);
        lastFrameTimes.gpuMs = renderer->getGpuMs();
        lastFrameTimes.gpuShadowMs = renderer->getGpuShadowMs();
        if (world) {
            debugOverlay.recordFrame(lastFrameTimes);
            if (benchmark) benchmark->recordFrame(lastFrameTimes, renderer->getStats(), world->getUpdateTimes());
        }
        lastFrameTimes = FrameTimes();

        // Cap deltaTime so a long stall (window drag, breakpoint, saving) doesn't run hundreds of physics steps
        const float maxDeltaTime = 0.25f;
        if (deltaTime > maxDeltaTime) {
            deltaTime = maxDeltaTime;
        }

        // Hand this frame's input to the UI and start collecting the next frame's
        UIInput input = pendingInput;
        pendingInput = UIInput();
        input.mouse = mousePosition;
        input.mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        input.time = currentFrame;

        if (escapePressed) {
            escapePressed = false;
            handleEscape();
        }

        if (benchmark && benchmark->isFinished()) {
            finishBenchmark();
        }

        bool playing = world && !menu.isOpen() && !benchmark;
        double updateStart = nowSeconds();
        if (playing) {
            updateGame(deltaTime);
        }

        // The benchmark flies its own camera, otherwise it's the player's
        glm::vec3 cameraPos(0.0f);
        glm::mat4 view(1.0f);
        float sunTime = currentFrame;
        if (benchmark) {
            benchmark->update(*world, frameSeconds);//uncapped, so the path takes the same real time on any machine
            cameraPos = benchmark->cameraPos();
            view = benchmark->viewMatrix();
            sunTime = Benchmark::sunTime;
        }
        else if (world) {
            cameraPos = player->getCameraPos();
            view = player->getViewMatrix();
        }

        if (world) {
            // Keeps loading chunks while paused, so the world is ready when you resume
            world->update(cameraPos);
        }
        lastFrameTimes.updateMs = static_cast<float>((nowSeconds() - updateStart) * 1000.0);

        // 3D world, or a plain background on the title screens
        double renderStart = nowSeconds();
        if (world) {
            float farPlane = (world->getRenderDistance() + 2) * Chunk::chunkSize * 1.5f;
            renderer->render(*world, cameraPos, view,
                width, height, sunTime, farPlane, playing && hasTarget ? &target : nullptr);
        }
        else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        lastFrameTimes.renderMs = static_cast<float>((nowSeconds() - renderStart) * 1000.0);

        // Before the HUD is drawn, so the screenshot is just the world
        if (benchmark) {
            std::string path = benchmark->screenshotPath();
            if (!path.empty() && !saveScreenshot(path, width, height)) std::cerr << "Couldn't save " << path << std::endl;
        }

        // UI on top
        if (menu.isOpen()) {
            ui->beginFrame(input, width, height);
            MenuAction action = menu.draw(*ui, settings, currentFrame);
            ui->endFrame();
            handleMenuAction(action);
        }
        else if (world) {
            drawHud(input);
        }

        // Settings are written once you leave the settings screen, not on every slider step
        if (settingsDirty && menu.current() != MenuScreen::Settings) {
            settings.save(SETTINGS_FILE);
            settingsDirty = false;
        }

        updateCursorMode();
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

int main(int argc, char** argv)
{
    Main main;
    // "OpenGlGame1.exe --benchmark" runs the benchmark straight away and closes when it's done
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--benchmark") main.runBenchmarkAndQuit();
    }

    main.run();

    return 0;
}
