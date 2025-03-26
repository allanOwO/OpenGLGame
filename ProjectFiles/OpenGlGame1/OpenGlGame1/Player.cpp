#include "Player.h"


Player::Player(GLFWwindow* window) 
    : window(window), velocity(0.0f), cameraFront(0.0f, 0.0f, -1.0f), cameraUp(0.0f, 1.0f, 0.0f), firstMouse(true),grounded(false) {


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
    cameraPos = spawnPos;
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
    float camSpeed = camSpeedBase * deltaTime;

    // Apply gravity
    velocity.y += gravity * deltaTime; // Accelerate downward

    // WASD Input
    glm::vec3 moveDir(0.0f);
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp)); // Right vector
    glm::vec3 forward = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z)); // Flatten forward vector

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        moveDir += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        moveDir -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        moveDir += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        moveDir -= right;

    // Normalize input and apply velocity
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        velocity.x = moveDir.x * camSpeed;
        velocity.z = moveDir.z * camSpeed;
    }
    else {
        velocity.x *= 0.9f; // Friction
        velocity.z *= 0.9f;
    }

    // Proposed new position
    glm::vec3 newPos = cameraPos + velocity * deltaTime;
    AABB playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };

    // Reset grounded state before collision checks
    grounded = false;

    // Calculate the player's chunk position
    glm::vec3 playerChunkPos = glm::floor(newPos / glm::vec3(Chunk::chunkSize, 1.0f, Chunk::chunkSize)) *
        glm::vec3(Chunk::chunkSize, 0.0f, Chunk::chunkSize);
    // Assuming y-coordinate is fixed or defined elsewhere (e.g., -Chunk::baseTerrainHeight)
    playerChunkPos.y = -Chunk::baseTerrainHeight; // Adjust this based on your world setup

    // Check the player's chunk and immediate neighbors
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            glm::vec3 checkChunkPos = playerChunkPos + glm::vec3(dx * Chunk::chunkSize, 0, dz * Chunk::chunkSize);
            auto it = chunks.find(checkChunkPos);
            if (it != chunks.end()) {
                const Chunk& chunk = it->second; // Access the Chunk object
                glm::vec3 chunkPos = it->first;  // Chunk position from the map key

                // Check blocks around the player
                for (int bx = -1; bx <= 1; bx++) {
                    for (int by = -1; by <= 1; by++) {
                        for (int bz = -1; bz <= 1; bz++) {
                            // World position of the block
                            glm::vec3 blockWorldPos = glm::floor(newPos) + glm::vec3(bx, by, bz);
                            // Local position within the chunk
                            int localX = static_cast<int>(blockWorldPos.x - chunkPos.x);
                            int localY = static_cast<int>(blockWorldPos.y - chunkPos.y);
                            int localZ = static_cast<int>(blockWorldPos.z - chunkPos.z);

                            // Ensure the block is within chunk bounds
                            if (localX >= 0 && localX < Chunk::chunkSize &&
                                localY >= 0 && localY < Chunk::chunkHeight &&
                                localZ >= 0 && localZ < Chunk::chunkSize) {
                                const Block& block = chunk.blocks[localX][localY][localZ];
                                if (block.type != BlockType::AIR) {
                                    AABB blockBox = {
                                        chunkPos + glm::vec3(localX, localY, localZ),
                                        chunkPos + glm::vec3(localX + 1, localY + 1, localZ + 1)
                                    };
                                    if (intersects(playerBox, blockBox)) {

                                        if (velocity.y < 0) { // Moving down
                                            // Check if the player's feet are colliding with a block's top
                                            if (playerBox.max.y > blockBox.min.y && playerBox.min.y < blockBox.max.y) {
                                                newPos.y = blockBox.max.y - playerAABB.min.y + 0.001f;
                                                velocity.y = 0.0f;
                                                grounded = true;
                                            }
                                        }
                                        else if (velocity.y > 0) { // Moving up
                                            // Check if the player's head is colliding with a block's bottom.
                                            if (playerBox.max.y > blockBox.min.y && playerBox.max.y < blockBox.max.y) {
                                                newPos.y = blockBox.min.y - playerAABB.max.y - 0.001f;
                                                velocity.y = 0.0f;
                                            }
                                        }

                                        // Horizontal collision (walls)
                                        if (velocity.x != 0 || velocity.z != 0) {
                                            if (playerBox.min.x < blockBox.max.x && playerBox.max.x > blockBox.min.x) {
                                                newPos.x = cameraPos.x;
                                                velocity.x = 0.0f;
                                            }
                                            if (playerBox.min.z < blockBox.max.z && playerBox.max.z > blockBox.min.z) {
                                                newPos.z = cameraPos.z;
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
        } 
    }

    // Update position
    cameraPos = newPos;

    // Jump logic
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && grounded) {
        velocity.y = 12.0f; // Jump strength
    }
}

// Helper function for AABB intersection
bool Player::intersects(const AABB& a, const AABB& b) {
    return (a.min.x < b.max.x && a.max.x > b.min.x &&
        a.min.y < b.max.y && a.max.y > b.min.y &&
        a.min.z < b.max.z && a.max.z > b.min.z);
}



glm::mat4 Player::getViewMatrix() const {
    return glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
}