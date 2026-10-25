#include "Player.h"
#include <cmath>
#include "World.h"


Player::Player(GLFWwindow* window) : window(window) {
}

void Player::spawn(glm::vec3 spawnPos) {
    bodyPos = spawnPos;
    previousBodyPos = spawnPos;
    cameraPos = bodyPos + glm::vec3(0, eyeLevel, 0);
}

void Player::update(float deltaTime, const World& world) {

    previousBodyPos = bodyPos;
    playerMovement(deltaTime, world);
}

void Player::interpolate(float alpha) {
    cameraPos = glm::mix(previousBodyPos, bodyPos, alpha) + glm::vec3(0.0f, eyeLevel, 0.0f);
}

//handles looking
void Player::processMouseMovement(double xpos, double ypos) {

    if (firstMouse)//stops large 1st jump
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    //calculate difference since last frame
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates range from bottom to top
    lastX = xpos;
    lastY = ypos;

    // Zoomed in, the same mouse movement would sweep across far more of the (now tiny) view,
    // so turning slows down by the zoom to feel the same on screen
    xoffset *= lookSens / zoom;
    yoffset *= lookSens / zoom;

    //add offset to stored rotation values
    yaw += xoffset;
    pitch += yoffset;

    updateCameraFront();
}

void Player::setLook(float newYaw, float newPitch) {
    yaw = newYaw;
    pitch = newPitch;
    updateCameraFront();
}

void Player::toggleZoom() {
    zoomed = !zoomed;
    targetZoom = defaultZoom;//every zoom starts at the same level
}

void Player::scrollZoom(float notches) {
    if (!zoomed || notches == 0.0f) return;
    // Multiplying per notch instead of adding means every notch feels like the same step,
    // 2x -> 2.5x is as noticeable as 8x -> 10x
    targetZoom = glm::clamp(targetZoom * std::pow(1.25f, notches), minZoom, maxZoom);
}

void Player::updateZoom(float deltaTime) {
    // Covers most of the gap each frame, the same fraction per second at any frame rate (like the friction)
    // Eased in log space for the same reason as scrollZoom, so 1x -> 4x doesn't rush the first half
    float target = getTargetZoom();
    float t = 1.0f - std::exp(-deltaTime * 15.0f);
    zoom = std::exp(glm::mix(std::log(zoom), std::log(target), t));
    if (std::abs(zoom - target) < 0.001f) zoom = target;
}

void Player::updateCameraFront() {
    //vertical clamping
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    //do the movement part
    glm::vec3 lookDirection;
    lookDirection.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    lookDirection.y = sin(glm::radians(pitch));
    lookDirection.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(lookDirection);//set cam front to new direcction
}

void Player::playerMovement(float deltaTime, const World& world) {
    float camSpeed = BASE_SPEED;

    // Apply gravity
    velocity.y += GRAVITY * deltaTime;

    // WASD Input
    glm::vec3 moveDir(0.0f);
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    glm::vec3 forward = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        camSpeed *= SPRINT_SPEED;
    }

    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        velocity.x = moveDir.x * camSpeed;
        velocity.z = moveDir.z * camSpeed;
    }
    else {
        // Friction, keeps 90% of speed every 1/60s regardless of step size
        float friction = pow(0.9f, deltaTime * 60.0f);
        velocity.x *= friction;
        velocity.z *= friction;
    }

    // Jump logic
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && grounded) {
        //https://medium.com/%40brazmogu/physics-for-game-dev-a-platformer-physics-cheatsheet-f34b09064558
        velocity.y = sqrt(2.0f * -GRAVITY * JUMP_HEIGHT);
    }

    // Move one axis at a time, so hitting a wall on X still lets you slide along Z
    glm::vec3 newPos = bodyPos;
    grounded = false;
    moveAxis(0, velocity.x * deltaTime, newPos, world);
    moveAxis(2, velocity.z * deltaTime, newPos, world);
    moveAxis(1, velocity.y * deltaTime, newPos, world);

    // Update position
    bodyPos = newPos;
    cameraPos = bodyPos + glm::vec3(0.0f, eyeLevel, 0.0f);  // Recalculate the camera position as an offset
}

// Moves along one axis (0 = x, 1 = y, 2 = z) then pushes the player back out of any block it ended up inside
void Player::moveAxis(int axis, float amount, glm::vec3& position, const World& world) {
    position[axis] += amount;
    AABB playerBox = { position + playerAABB.min, position + playerAABB.max };

    // Every block the player's box overlaps
    glm::ivec3 minBlock = glm::ivec3(glm::floor(playerBox.min));
    glm::ivec3 maxBlock = glm::ivec3(glm::floor(playerBox.max));

    for (int x = minBlock.x; x <= maxBlock.x; x++) {
        for (int y = minBlock.y; y <= maxBlock.y; y++) {
            for (int z = minBlock.z; z <= maxBlock.z; z++) {
                glm::ivec3 block(x, y, z);
                if (world.getBlock(block) == BlockType::AIR) continue;

                AABB blockBox = { glm::vec3(block), glm::vec3(block) + glm::vec3(1.0f) };
                if (!intersects(playerBox, blockBox)) continue;

                // Snap to the side of the block we came from, small gap so we aren't touching next step
                if (amount > 0) {
                    position[axis] = blockBox.min[axis] - playerAABB.max[axis] - 0.001f;
                }
                else if (amount < 0) {
                    position[axis] = blockBox.max[axis] - playerAABB.min[axis] + 0.001f;
                    if (axis == 1) grounded = true;// Falling onto a block
                }
                velocity[axis] = 0.0f;
                playerBox = { position + playerAABB.min, position + playerAABB.max };
            }
        }
    }
}

// Helper function for AABB intersection
bool Player::intersects(const AABB& a, const AABB& b) const {
    return (a.min.x < b.max.x && a.max.x > b.min.x &&
        a.min.y < b.max.y && a.max.y > b.min.y &&
        a.min.z < b.max.z && a.max.z > b.min.z);
}

bool Player::blockIntersects(const glm::vec3& blockPos) const {
    // Create the block's AABB (assuming the block is 1x1x1)
    AABB blockAABB(blockPos, blockPos + glm::vec3(1.0f, 1.0f, 1.0f));
    AABB playerBox = { bodyPos + playerAABB.min, bodyPos + playerAABB.max };

    // Use the member function 'intersects' to compare the block's AABB with the player's AABB
    return intersects(blockAABB, playerBox);
}



glm::mat4 Player::getViewMatrix() const {
    return glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
}
