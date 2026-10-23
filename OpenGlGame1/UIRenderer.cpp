#include "UIRenderer.h"
#include <cstddef>//offsetof
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Font.h"
#include "ResourcePath.h"

UIRenderer::UIRenderer() {
    shader = std::make_unique<Shader>(getResourcePath("ui_vertex.glsl").c_str(), getResourcePath("ui_fragment.glsl").c_str());
    projectionLoc = glGetUniformLocation(shader->ID, "projection");
    shader->use();
    glUniform1i(glGetUniformLocation(shader->ID, "fontTexture"), 0);

    // Font texture, one channel (GL_RED) since each pixel is just on or off
    std::vector<uint8_t> pixels = Font::buildTexture();
    glGenTextures(1, &fontTexture);
    glBindTexture(GL_TEXTURE_2D, fontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);//rows are tightly packed bytes
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, Font::textureWidth, Font::textureHeight, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // Nearest keeps the pixel font crisp when scaled up
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

UIRenderer::~UIRenderer() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &fontTexture);
}

void UIRenderer::begin(int screenWidth, int screenHeight) {
    width = screenWidth;
    height = screenHeight;
    vertices.clear();
}

void UIRenderer::quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const glm::vec4& colour) {
    // Two triangles, no index buffer needed for this little data
    Vertex topLeft{ x, y, u0, v0, colour.r, colour.g, colour.b, colour.a };
    Vertex topRight{ x + w, y, u1, v0, colour.r, colour.g, colour.b, colour.a };
    Vertex bottomRight{ x + w, y + h, u1, v1, colour.r, colour.g, colour.b, colour.a };
    Vertex bottomLeft{ x, y + h, u0, v1, colour.r, colour.g, colour.b, colour.a };
    vertices.insert(vertices.end(), { topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight });
}

void UIRenderer::rect(float x, float y, float w, float h, const glm::vec4& colour) {
    // Sample the middle of the white cell, so every pixel of the rectangle reads 1.0
    int cell = Font::whiteCell - Font::firstChar;
    float u = ((cell % Font::columns) * Font::cellSize + Font::cellSize * 0.5f) / Font::textureWidth;
    float v = ((cell / Font::columns) * Font::cellSize + Font::cellSize * 0.5f) / Font::textureHeight;
    quad(x, y, w, h, u, v, u, v, colour);
}

void UIRenderer::text(const std::string& str, float x, float y, int scale, const glm::vec4& colour) {
    // Whole pixel positions keep the font from blurring or losing rows
    float penX = std::round(x);
    float penY = std::round(y);
    for (char c : str) {
        int cell = Font::cellFor(c) - Font::firstChar;
        float u0 = static_cast<float>((cell % Font::columns) * Font::cellSize) / Font::textureWidth;
        float v0 = static_cast<float>((cell / Font::columns) * Font::cellSize) / Font::textureHeight;
        float u1 = u0 + static_cast<float>(Font::glyphWidth) / Font::textureWidth;
        float v1 = v0 + static_cast<float>(Font::glyphHeight) / Font::textureHeight;
        if (c != ' ') {
            quad(penX, penY, Font::glyphWidth * scale, Font::glyphHeight * scale, u0, v0, u1, v1, colour);
        }
        penX += Font::advance * scale;
    }
}

float UIRenderer::textWidth(const std::string& str, int scale) {
    if (str.empty()) return 0.0f;
    // No gap after the last letter
    return static_cast<float>((str.size() * Font::advance - 1) * scale);
}

float UIRenderer::textHeight(int scale) {
    return static_cast<float>(Font::glyphHeight * scale);
}

void UIRenderer::flush() {
    if (vertices.empty()) return;

    // UI draws over everything, in the order it was added, with transparency
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, width, height);

    shader->use();
    // Pixel coordinates: top left (0,0), y going down like most UI tools
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTexture);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // Whole buffer is replaced every frame
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);

    // Put back what the 3D renderer expects
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    vertices.clear();
}
