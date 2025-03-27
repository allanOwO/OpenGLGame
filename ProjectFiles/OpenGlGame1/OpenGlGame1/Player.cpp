#include "Player.h"
#include <iostream>//print



Player::Player(GLFWwindow* window) 
    : window(window), velocity(0.0f), bodyPos(0.0f), cameraFront(0.0f, 0.0f, -1.0f), cameraUp(0.0f, 1.0f, 0.0f), firstMouse(true),grounded(false) {


    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Set GLFW window user pointer to this instance
    glfwSetWindowUserPointer(window, this);

    // Register a lambda for the mouse callback
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        Player* player = static_cast<Player*>(glfwGetWindowUserPointer(window));
        if (player) {
            player->processMouseMovement(window,static_cast<float>(xpos), static_cast<float>(ypos)); 
        }
     });
}

void Player::spawn(glm::vec3 spawnPos) {
    bodyPos = spawnPos;
    cameraPos = bodyPos + glm::vec3(0, eyeLevel, 0);
}

void Player::update(float deltaTime, const std::unordered_map<glm::vec3, Chunk, Vec3Hash>& chunks) {

    playerMovement(deltaTime, chunks);
}

//handles looking
void Player::processMouseMovement(GLFWwindow* window, double xpos, double ypos) {

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

    xoffset *= lookSens;
    yoffset *= lookSens;

    //add offset to stored rotation values
    yaw += xoffset;
    pitch += yoffset;

    //vertical clamping
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    //do the movement part
    lookDirection.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    lookDirection.y = sin(glm::radians(pitch));
    lookDirection.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(lookDirection);//set cam front to new direcction
}

void Player::playerMovement(float deltaTime, const std::unordered_map<glm::vec3, Chunk, Vec3Hash>& chunks) {
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
        velocity.x *= 0.9f; // Friction
        velocity.z *= 0.9f;
    }

    // Jump logic 
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && grounded) { 
        //https://medium.com/%40brazmogu/physics-for-game-dev-a-platformer-physics-cheatsheet-f34b09064558
        velocity.y = sqrt(2.0f * -GRAVITY * JUMP_HEIGHT);
    }

    // Proposed new position (step-by-step for each axis) 
    glm::vec3 newPos = bodyPos;
    AABB playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };
    grounded = false;

    // Player's chunk position
    glm::vec3 playerChunkPos = glm::floor(newPos / glm::vec3(Chunk::chunkSize, 1.0f, Chunk::chunkSize)) *
        glm::vec3(Chunk::chunkSize, 0.0f, Chunk::chunkSize);
    playerChunkPos.y = -Chunk::baseTerrainHeight;

    // Step 1: Resolve X-axis
    newPos.x += velocity.x * deltaTime;
    playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            glm::vec3 checkChunkPos = playerChunkPos + glm::vec3(dx * Chunk::chunkSize, 0, dz * Chunk::chunkSize);
            auto it = chunks.find(checkChunkPos);
            if (it != chunks.end()) {
                const Chunk& chunk = it->second;
                glm::vec3 chunkPos = it->first;
                for (int bx = -1; bx <= 1; bx++) {
                    for (int by = -1; by <= 1; by++) {
                        for (int bz = -1; bz <= 1; bz++) {
                            glm::vec3 blockWorldPos = glm::floor(newPos) + glm::vec3(bx, by, bz);
                            int localX = static_cast<int>(blockWorldPos.x - chunkPos.x);
                            int localY = static_cast<int>(blockWorldPos.y - chunkPos.y);
                            int localZ = static_cast<int>(blockWorldPos.z - chunkPos.z);
                            if (localX >= 0 && localX < Chunk::chunkSize &&
                                localY >= 0 && localY < Chunk::chunkHeight &&
                                localZ >= 0 && localZ < Chunk::chunkSize) {


                                size_t index = chunk.getBlockIndex(localX, localY, localZ);

                                if (chunk.blocks[index] != BlockType::AIR) {
                                    AABB blockBox = {
                                        chunkPos + glm::vec3(localX, localY, localZ),
                                        chunkPos + glm::vec3(localX + 1, localY + 1, localZ + 1)
                                    };
                                    if (intersects(playerBox, blockBox)) {
                                        if (velocity.x > 0) {
                                            newPos.x = blockBox.min.x - playerAABB.max.x - 0.001f;
                                        }
                                        else if (velocity.x < 0) {
                                            newPos.x = blockBox.max.x - playerAABB.min.x + 0.001f;
                                        }
                                        velocity.x = 0.0f;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Step 2: Resolve Z-axis
    newPos.z += velocity.z * deltaTime;
    playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            glm::vec3 checkChunkPos = playerChunkPos + glm::vec3(dx * Chunk::chunkSize, 0, dz * Chunk::chunkSize);
            auto it = chunks.find(checkChunkPos);
            if (it != chunks.end()) {
                const Chunk& chunk = it->second;
                glm::vec3 chunkPos = it->first;
                for (int bx = -1; bx <= 1; bx++) {
                    for (int by = -1; by <= 1; by++) {
                        for (int bz = -1; bz <= 1; bz++) {
                            glm::vec3 blockWorldPos = glm::floor(newPos) + glm::vec3(bx, by, bz);
                            int localX = static_cast<int>(blockWorldPos.x - chunkPos.x);
                            int localY = static_cast<int>(blockWorldPos.y - chunkPos.y);
                            int localZ = static_cast<int>(blockWorldPos.z - chunkPos.z);
                            if (localX >= 0 && localX < Chunk::chunkSize &&
                                localY >= 0 && localY < Chunk::chunkHeight &&
                                localZ >= 0 && localZ < Chunk::chunkSize) {

                                size_t index = chunk.getBlockIndex(localX, localY, localZ);

                                if (chunk.blocks[index] != BlockType::AIR) { 
                                    AABB blockBox = {
                                        chunkPos + glm::vec3(localX, localY, localZ),
                                        chunkPos + glm::vec3(localX + 1, localY + 1, localZ + 1)
                                    };
                                    if (intersects(playerBox, blockBox)) {
                                        if (velocity.z > 0) {
                                            newPos.z = blockBox.min.z - playerAABB.max.z - 0.001f;
                                        }
                                        else if (velocity.z < 0) {
                                            newPos.z = blockBox.max.z - playerAABB.min.z + 0.001f;
                                        }
                                        velocity.z = 0.0f;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Step 3: Resolve Y-axis
    newPos.y += velocity.y * deltaTime;
    playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            glm::vec3 checkChunkPos = playerChunkPos + glm::vec3(dx * Chunk::chunkSize, 0, dz * Chunk::chunkSize);
            auto it = chunks.find(checkChunkPos);
            if (it != chunks.end()) {
                const Chunk& chunk = it->second;
                glm::vec3 chunkPos = it->first;
                for (int bx = -1; bx <= 1; bx++) {
                    for (int by = -1; by <= 1; by++) {
                        for (int bz = -1; bz <= 1; bz++) {
                            glm::vec3 blockWorldPos = glm::floor(newPos) + glm::vec3(bx, by, bz);
                            int localX = static_cast<int>(blockWorldPos.x - chunkPos.x);
                            int localY = static_cast<int>(blockWorldPos.y - chunkPos.y);
                            int localZ = static_cast<int>(blockWorldPos.z - chunkPos.z);
                            if (localX >= 0 && localX < Chunk::chunkSize &&
                                localY >= 0 && localY < Chunk::chunkHeight &&
                                localZ >= 0 && localZ < Chunk::chunkSize) {

                                size_t index = chunk.getBlockIndex(localX, localY, localZ);

                                if (chunk.blocks[index] != BlockType::AIR) { 
                                    AABB blockBox = {
                                        chunkPos + glm::vec3(localX, localY, localZ),
                                        chunkPos + glm::vec3(localX + 1, localY + 1, localZ + 1)
                                    };
                                    if (intersects(playerBox, blockBox)) {
                                        if (velocity.y < 0) { // Falling
                                            newPos.y = blockBox.max.y - playerAABB.min.y + 0.001f;
                                            velocity.y = 0.0f;
                                            grounded = true;
                                        }
                                        else if (velocity.y > 0) { // Jumping
                                            newPos.y = blockBox.min.y - playerAABB.max.y - 0.001f;
                                            velocity.y = 0.0f;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Update position
    bodyPos = newPos;
    cameraPos = bodyPos + glm::vec3(0.0f, eyeLevel, 0.0f);  // Recalculate the camera position as an offset
    

    
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