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

void Player::update(float deltaTime, std::vector<Chunk>& chunks) {

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

void Player::playerMovement(float deltaTime,std::vector<Chunk>& chunks) {

    //look movement
    
    float camSpeed = camSpeedBase * deltaTime;

    // Apply gravity and movemnt
    velocity.y += gravity * deltaTime; // Accelerate downward  

    //WASD INPUT
    glm::vec3 moveDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        moveDir += camSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        moveDir -= camSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        moveDir += glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        moveDir -= glm::normalize(glm::cross(cameraFront, cameraUp)) * camSpeed;

    //normalise input
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        velocity.x = moveDir.x * camSpeedBase;
        velocity.z = moveDir.z * camSpeedBase;
    }
    else {
        velocity.x *= 0.9f; // Friction to slow down
        velocity.z *= 0.9f;
    }
    //proposed new pos
    glm::vec3 newPos = cameraPos + velocity * deltaTime;

    // Collision detection
    AABB playerBox = { newPos + playerAABB.min, newPos + playerAABB.max };
    bool collided = false;

    // Check nearby chunks (simplified to current chunk for now)
    int chunkX = static_cast<int>(floor(newPos.x / Chunk::chunkSize));
    int chunkZ = static_cast<int>(floor(newPos.z / Chunk::chunkSize));

    for (auto& chunk : chunks) {
        glm::vec3 chunkPos = chunk.chunkPosition;
        if (static_cast<int>(chunkPos.x / Chunk::chunkSize) == chunkX &&
            static_cast<int>(chunkPos.z / Chunk::chunkSize) == chunkZ) {
            for (int x = -1; x <= 1; x++) {
                for (int y = -1; y <= 1; y++) {
                    for (int z = -1; z <= 1; z++) {
                        int localX = static_cast<int>(newPos.x - chunkPos.x) + x;
                        int localY = static_cast<int>(newPos.y - chunkPos.y) + y;
                        int localZ = static_cast<int>(newPos.z - chunkPos.z) + z;

                        if (localX >= 0 && localX < Chunk::chunkSize &&
                            localY >= 0 && localY < Chunk::chunkHeight &&
                            localZ >= 0 && localZ < Chunk::chunkSize) {
                            if (chunk.blocks[localX][localY][localZ].type != BlockType::AIR) {
                                AABB blockBox = {
                                    chunkPos + glm::vec3(localX, localY, localZ),
                                    chunkPos + glm::vec3(localX + 1, localY + 1, localZ + 1)
                                };
                                if (intersects(playerBox, blockBox)) {
                                    // Vertical collision (ground or ceiling)
                                    if (velocity.y < 0 && playerBox.max.y > blockBox.min.y &&
                                        playerBox.min.y < blockBox.max.y) {
                                        newPos.y = blockBox.max.y - playerAABB.min.y + 0.001f;
                                        velocity.y = 0.0f;
                                        grounded = true;

                                    }
                                    else if (velocity.y > 0 && playerBox.min.y < blockBox.max.y) {
                                        newPos.y = blockBox.min.y - playerAABB.max.y + 0.001f;
                                        velocity.y = 0.0f;
                                    }
                                    // Horizontal collision (walls)
                                    else if (velocity.x != 0 || velocity.z != 0) {
                                        if (playerBox.min.x < blockBox.max.x && playerBox.max.x > blockBox.min.x) {
                                            newPos.x = cameraPos.x; // Revert X movement
                                            velocity.x = 0.0f;
                                        }
                                        if (playerBox.min.z < blockBox.max.z && playerBox.max.z > blockBox.min.z) {
                                            newPos.z = cameraPos.z; // Revert Z movement
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

    cameraPos = newPos; // Update position after collision checks

    // Add jump
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && grounded) {
        velocity.y = 4.0f; // Jump strength, adjust as needed
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