#include "Renderer.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION  // This tells stb to include the implementation
#include "stb/stb_image.h"

#include "World.h"
#include "ResourcePath.h"

static const float SUN_TILT = glm::radians(70.0f);
static const float SUN_SPEED = 0.1f;

Renderer::Renderer() {
    createShadowMap();
    createSun();
    loadTextures();
    createShaders();
    createHighlight();
}

Renderer::~Renderer() {
    // Shaders delete themselves (unique_ptr), the rest are raw GL objects
    glDeleteVertexArrays(1, &sunVAO);
    glDeleteBuffers(1, &sunVBO);
    glDeleteBuffers(1, &sunEBO);
    glDeleteVertexArrays(1, &highlightVAO);
    glDeleteBuffers(1, &highlightVBO);
    glDeleteBuffers(1, &highlightEBO);
    glDeleteTextures(1, &texAtlas);
    glDeleteTextures(1, &depthMap);
    glDeleteFramebuffers(1, &depthMapFBO);
}

// ---------- setup ----------

void Renderer::createShaders() {
    shader = std::make_unique<Shader>(getResourcePath("vertex_shader.glsl").c_str(), getResourcePath("fragment_shader.glsl").c_str());
    sunShader = std::make_unique<Shader>(getResourcePath("sunVertex.glsl").c_str(), getResourcePath("sunFragment.glsl").c_str());
    depthShader = std::make_unique<Shader>(getResourcePath("depth_vertex.glsl").c_str(), getResourcePath("depth_fragment.glsl").c_str());
    highlightShader = std::make_unique<Shader>(getResourcePath("highlight_vertex.glsl").c_str(), getResourcePath("highlight_fragment.glsl").c_str());

    shader->use();
    viewLocation = glGetUniformLocation(shader->ID, "view");
    projectionLocation = glGetUniformLocation(shader->ID, "projection");
    textureLocation = glGetUniformLocation(shader->ID, "ourTexture");
    sunDirLoc = glGetUniformLocation(shader->ID, "sunDirection");
    camPosLoc = glGetUniformLocation(shader->ID, "cameraPos");
    shadowMapLoc = glGetUniformLocation(shader->ID, "shadowMap");
    mainLightSpaceLoc = glGetUniformLocation(shader->ID, "lightSpaceMatrix");

    // Samplers never change unit, set once: atlas on 0, shadow map on 1
    glUniform1i(textureLocation, 0);
    glUniform1i(shadowMapLoc, 1);

    sunViewLoc = glGetUniformLocation(sunShader->ID, "view");
    sunProjLoc = glGetUniformLocation(sunShader->ID, "projection");
    sunSunDirLoc = glGetUniformLocation(sunShader->ID, "sunDirection");
    sunColourLoc = glGetUniformLocation(sunShader->ID, "sunColour");
    sunCamPosLoc = glGetUniformLocation(sunShader->ID, "camPos");

    lightSpaceLoc = glGetUniformLocation(depthShader->ID, "lightSpaceMatrix");
    if (lightSpaceLoc == -1) {
        std::cerr << "Uniform 'lightSpaceMatrix' not found in depth shader" << std::endl;
    }

    highlightModelLoc = glGetUniformLocation(highlightShader->ID, "model");
    highlightViewLoc = glGetUniformLocation(highlightShader->ID, "view");
    highlightProjLoc = glGetUniformLocation(highlightShader->ID, "projection");
    highlightColourLoc = glGetUniformLocation(highlightShader->ID, "colour");
}

void Renderer::loadTextures() {

    int width, height, nrChannels;

    unsigned char* data = stbi_load(getResourcePath("textureAtlas.png").c_str(), &width, &height, &nrChannels, 0);
    if (!data) {
        std::cerr << "Failed to load atlas" << std::endl;
        exit(-1);
    }

    // Determine texture format based on loaded image
    GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;

    glGenTextures(1, &texAtlas);
    glBindTexture(GL_TEXTURE_2D, texAtlas);
    //wrap and fill params
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
}

void Renderer::createShadowMap() {
    // Generate and configure depth texture
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Generate and configure framebuffer
    glGenFramebuffers(1, &depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE); // No color buffer for shadow map
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow map framebuffer incomplete: " << status << std::endl;
        switch (status) {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            std::cerr << "Incomplete attachment" << std::endl; break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            std::cerr << "Missing attachment" << std::endl; break;
        default:
            std::cerr << "Other error" << std::endl; break;
        }
        exit(-1); // Or handle gracefully
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::createSun() {
    // Vertex data for sun quad
    float sunVertices[] = {
        -0.5f,  0.5f,  0.0f, 1.0f, // Top-left
         0.5f,  0.5f,  1.0f, 1.0f, // Top-right
         0.5f, -0.5f,  1.0f, 0.0f, // Bottom-right
        -0.5f, -0.5f,  0.0f, 0.0f  // Bottom-left
    };
    unsigned int sunIndices[] = { 0, 3, 2, 2, 1, 0 };//reversed winding order

    glGenVertexArrays(1, &sunVAO);
    glGenBuffers(1, &sunVBO);
    glGenBuffers(1, &sunEBO);

    glBindVertexArray(sunVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sunVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void Renderer::createHighlight() {
    float vertices[] = {
        -0.51f, -0.51f, -0.51f,  0.51f, -0.51f, -0.51f,
         0.51f,  0.51f, -0.51f, -0.51f,  0.51f, -0.51f,
        -0.51f, -0.51f,  0.51f,  0.51f, -0.51f,  0.51f,
         0.51f,  0.51f,  0.51f, -0.51f,  0.51f,  0.51f
    };
    unsigned int indices[] = {
        0, 1, 1, 2, 2, 3, 3, 0, // Bottom face
        4, 5, 5, 6, 6, 7, 7, 4, // Top face
        0, 4, 1, 5, 2, 6, 3, 7  // Sides
    };

    glGenVertexArrays(1, &highlightVAO);
    glGenBuffers(1, &highlightVBO);
    glGenBuffers(1, &highlightEBO);

    glBindVertexArray(highlightVAO);
    glBindBuffer(GL_ARRAY_BUFFER, highlightVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlightEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

// ---------- per frame ----------

glm::mat4 Renderer::projection(float zoom, float aspect, float farPlane) {
    // Zooming is just a narrower field of view: half the view's height is tan(fov / 2) at distance 1,
    // so to make things zoom times bigger that has to be zoom times smaller
    const float baseFov = glm::radians(90.0f);
    float fov = 2.0f * std::atan(std::tan(baseFov * 0.5f) / zoom);
    return glm::perspective(fov, aspect, 0.1f, farPlane);
}

void Renderer::render(const World& world, const glm::vec3& cameraPos, const glm::mat4& view,
    int width, int height, float time, float farPlane, float zoom, const RaycastHit* highlight) {


    stats = RenderStats();

    glm::mat4 projection = Renderer::projection(zoom, (float)width / (float)height, farPlane);

    //whats in current view
    glm::mat4 viewProj = projection * view;
    frustum.update(viewProj); // Update frustum for culling

    // Rotate sun direction based on time
    float angle = time * SUN_SPEED;
    glm::vec3 sunDir = glm::normalize(glm::vec3(cos(angle), sin(angle) * sin(SUN_TILT), sin(angle) * cos(SUN_TILT))); // Compute sun direction using a circular motion
    sunDirection = -sunDir;

    //shadow code, smaller area gives better shadows with same resolution
    //shadow camera sits towards the sun and looks at the player, the box is centred on the player
    glm::mat4 lightProjection = glm::ortho(-SHADOW_RADIUS, SHADOW_RADIUS, -SHADOW_RADIUS, SHADOW_RADIUS,
        1.0f, SHADOW_LIGHT_DISTANCE * 2.0f);
    glm::vec3 lightPos = cameraPos + sunDir * SHADOW_LIGHT_DISTANCE;
    glm::mat4 lightView = glm::lookAt(lightPos, cameraPos, glm::vec3(0.0, 1.0, 0.0));

    // Snap the box to whole shadow map texels so shadow edges don't shimmer as the player moves
    glm::mat4 shadowMatrix = lightProjection * lightView;
    glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float texelsPerUnit = SHADOW_WIDTH * 0.5f;
    glm::vec2 snapOffset = (glm::round(glm::vec2(shadowOrigin) * texelsPerUnit) - glm::vec2(shadowOrigin) * texelsPerUnit) / texelsPerUnit;
    lightProjection[3][0] += snapOffset.x;
    lightProjection[3][1] += snapOffset.y;
    lightSpaceMatrix = lightProjection * lightView;

    // Below the horizon the shader ignores sunlight, so shadows wouldn't show anyway
    bool sunUp = sunDir.y > -0.1f;
    if (sunUp) {
        shadowTimer.begin();
        renderShadowMap(world);
        shadowTimer.end();
    }
    else {
        shadowTimer.skip();
    }
    mainTimer.begin();

    // Back to the default framebuffer for the main pass
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //use normal shader for chunks
    shader->use();

    // pass view and projection matrices to shader
    glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(camPosLoc, 1, glm::value_ptr(cameraPos));
    glUniform3fv(sunDirLoc, 1, glm::value_ptr(sunDirection));
    glUniformMatrix4fv(mainLightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    // Bind shadow map to texture unit 1 (unit 0 is for texture atlas, bound in drawChunks)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depthMap);

    drawChunks(world, cameraPos);

    if (highlight) {
        drawHighlight(*highlight, view, projection);
    }

    renderSun(view, projection, -sunDirection, cameraPos);
    mainTimer.end();
}

void Renderer::renderShadowMap(const World& world) {
    // Render depth map from the sun's point of view
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glDepthMask(GL_TRUE); // Ensure depth writes are enabled
    glClear(GL_DEPTH_BUFFER_BIT);

    depthShader->use();
    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    // Only chunks inside the light's box can cast shadows we see
    Frustum lightFrustum;
    lightFrustum.update(lightSpaceMatrix);

    //render chunks 1st pass to create shadows, depth only so the order doesn't matter
    collectVisible(world, lightFrustum, glm::vec3(0.0f), false, false);
    // From the sun, faces pointing away from it are skipped (towards the sun is -sunDirection)
    stats.shadowTriangles += drawList(world, nullptr, sunDirection);
    if (!visibleSections.empty()) stats.shadowDrawCalls++;
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::drawChunks(const World& world, const glm::vec3& cameraPos) {
    shader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texAtlas);

    // Find what's in view first, then draw nearest first
    // Near chunks fill the depth buffer, so pixels of far chunks hidden behind them fail the depth test
    // before the fragment shader runs, instead of being lit and shadowed and then drawn over
    collectVisible(world, frustum, cameraPos, true, true);

    stats.triangles = drawList(world, &cameraPos, glm::vec3(0.0f));
    if (!visibleSections.empty()) stats.drawCalls++;
    stats.drawCommands = static_cast<int>(drawCommands.size());
    glBindVertexArray(0);
}

void Renderer::collectVisible(const World& world, const Frustum& view, const glm::vec3& cameraPos, bool nearestFirst, bool countStats) {
    // Regions first: one test can skip 64 chunks, and a region fully inside the view needs no per chunk tests
    // (at render distance 64 that's ~300 region tests instead of 16k chunk tests, and the shadow pass skips nearly all of them)
    const float regionWidth = static_cast<float>(ChunkRegion::regionSize * Chunk::chunkSize);
    const float bottom = static_cast<float>(-Chunk::baseTerrainHeight);
    const float top = bottom + Chunk::chunkHeight;

    visibleRegions.clear();
    for (const auto& pair : world.getRegions()) {
        const ChunkRegion& region = pair.second;
        glm::vec3 min(region.coords.x * regionWidth, bottom, region.coords.y * regionWidth);
        glm::vec3 max = min + glm::vec3(regionWidth, top - bottom, regionWidth);
        Frustum::BoxResult result = view.classifyBox(min, max);
        if (result == Frustum::BoxResult::Outside) {
            if (countStats) stats.regionsCulled++;
            continue;
        }
        float dx = min.x + regionWidth * 0.5f - cameraPos.x;
        float dz = min.z + regionWidth * 0.5f - cameraPos.z;
        visibleRegions.push_back({ dx * dx + dz * dz, result == Frustum::BoxResult::Inside, &region });
    }
    if (nearestFirst) {
        std::sort(visibleRegions.begin(), visibleRegions.end(), [](const VisibleRegion& a, const VisibleRegion& b) {
            return a.distanceSquared < b.distanceSquared;
        });
    }

    visibleSections.clear();
    for (const VisibleRegion& entry : visibleRegions) {
        // Vertices are relative to their region (see packPlace), so every draw in it uses the region's corner
        glm::vec3 regionOrigin = entry.region->origin();

        // Everything far away in the region is one mesh, culled with the region: its triangles are cheap, draws aren't
        const MeshAllocation& merged = entry.region->merged;
        if (merged.vertexCount > 0) {
            float width = static_cast<float>(ChunkRegion::regionSize * Chunk::chunkSize);
            glm::vec2 corner(regionOrigin.x, regionOrigin.z);
            visibleSections.push_back({ &merged, regionOrigin, corner, corner + glm::vec2(width) });
            if (countStats) stats.mergedRegionsDrawn++;
        }

        for (const Chunk* chunk : entry.region->chunks) {
            uint32_t meshed = chunk->meshedSections & ~chunk->mergedSections;//merged ones were just drawn above
            if (!chunk->isActive || meshed == 0) continue;

            // Whole column next, from its lowest to highest drawn section, same idea as the regions
            bool testSections = false;
            if (!entry.fullyInside) {
                int lowest = 0, highest = Chunk::sectionCount - 1;
                while (!(meshed & (1u << lowest))) lowest++;
                while (!(meshed & (1u << highest))) highest--;
                glm::vec3 min = chunk->sectionOrigin(lowest);
                glm::vec3 max = chunk->sectionOrigin(highest) + glm::vec3(ChunkSection::size);
                Frustum::BoxResult result = view.classifyBox(min, max);
                if (result == Frustum::BoxResult::Outside) {
                    if (countStats) stats.sectionsCulled++;
                    continue;
                }
                testSections = result == Frustum::BoxResult::Partly;
            }

            // Only the sections whose bit is set, so empty sky and buried rock cost nothing
            for (uint32_t bits = meshed; bits != 0; bits &= bits - 1) {//clears the lowest set bit each time
                int s = 0;
                while (!(bits & (1u << s))) s++;
                const MeshAllocation& mesh = chunk->sections[s].mesh;
                glm::vec3 origin = chunk->sectionOrigin(s);
                if (testSections && !view.isBoxInFrustum(origin, origin + glm::vec3(ChunkSection::size))) {
                    if (countStats) stats.sectionsCulled++;
                    continue;
                }
                glm::vec2 corner(origin.x, origin.z);
                visibleSections.push_back({ &mesh, regionOrigin, corner, corner + glm::vec2(static_cast<float>(ChunkSection::size)) });
                if (countStats) {
                    stats.sectionsDrawn++;
                    stats.sectionsByLod[chunk->sections[s].meshLod]++;
                }
            }
        }
    }
}

int64_t Renderer::drawList(const World& world, const glm::vec3* viewPoint, const glm::vec3& viewDirection) {
    // One command per run of visible face groups: which vertices (baseVertex) and which origin (baseInstance)
    // The GPU runs through the list from one call per mesh pool page, instead of the CPU issuing a draw per mesh
    drawCommands.clear();
    drawOrigins.clear();
    drawPages.clear();
    int64_t triangles = 0;
    for (const VisibleSection& entry : visibleSections) {
        // A face pointing +X can only be seen from its +X side. Every +X face in this mesh is at or past min.x,
        // so with the viewer at or before min.x none of them can be: the whole group is skipped
        // From the sun every face in a group points the same way relative to it, so it's all or nothing
        bool visible[faceGroupCount];
        if (viewPoint) {
            visible[NegXFaces] = viewPoint->x < entry.max.x;
            visible[PosXFaces] = viewPoint->x > entry.min.x;
            visible[NegZFaces] = viewPoint->z < entry.max.y;
            visible[PosZFaces] = viewPoint->z > entry.min.y;
        }
        else {
            visible[NegXFaces] = viewDirection.x > 0.0f;//a -X face faces the viewer when the viewer looks along +X
            visible[PosXFaces] = viewDirection.x < 0.0f;
            visible[NegZFaces] = viewDirection.z > 0.0f;
            visible[PosZFaces] = viewDirection.z < 0.0f;
        }
        visible[VerticalFaces] = true;

        // Groups next to each other in the mesh that are both visible go in one command
        // A small hidden group between two visible ones is drawn anyway: a draw costs the GPU about as much
        // as ~400 triangles, so splitting around fewer than that costs more than it saves
        const GLsizei splitAtLeast = 800;//vertices (200 quads, 400 triangles)
        const MeshAllocation& mesh = *entry.mesh;
        GLint groupStart = mesh.firstVertex;
        GLint runStart = 0;
        GLsizei runLength = 0;
        GLsizei gap = 0;//hidden vertices since the last visible group of the run
        auto addRun = [&]() {
            if (runLength == 0) return;
            DrawCommand command;
            command.count = static_cast<GLuint>(runLength / 4 * 6);
            command.instanceCount = 1;
            command.firstIndex = 0;
            command.baseVertex = runStart;
            command.baseInstance = static_cast<GLuint>(drawCommands.size());
            drawCommands.push_back(command);
            drawOrigins.push_back(entry.origin);
            drawPages.push_back(mesh.page);
            triangles += runLength / 2;
            runLength = 0;
        };
        for (int g = 0; g < faceGroupCount; g++) {
            GLsizei count = mesh.groupCounts[g];
            if (count == 0) continue;
            if (visible[g]) {
                if (runLength == 0) runStart = groupStart;
                runLength += gap + count;//the hidden groups in between come along
                gap = 0;
            }
            else if (runLength > 0 && gap + count < splitAtLeast) {
                gap += count;//maybe drawn, if another visible group follows
            }
            else {
                addRun();//too much hidden in between, split
                gap = 0;
            }
            groupStart += count;
        }
        addRun();//a hidden gap at the end is just left off
    }

    world.getMeshPool().draw(drawCommands, drawOrigins, drawPages);
    return triangles;
}

void Renderer::drawHighlight(const RaycastHit& highlight, const glm::mat4& view, const glm::mat4& projection) {
    highlightShader->use();
    // Center the highlight by adding (0.5, 0.5, 0.5) to the block position
    glm::mat4 highlightModel = glm::translate(glm::mat4(1.0f), glm::vec3(highlight.block) + glm::vec3(0.5f, 0.5f, 0.5f));
    glUniformMatrix4fv(highlightModelLoc, 1, GL_FALSE, glm::value_ptr(highlightModel));
    glUniformMatrix4fv(highlightViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(highlightProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform4f(highlightColourLoc, 1.0f, 1.0f, 0.0f, 1.0f); // Yellow highlight

    glBindVertexArray(highlightVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0); // 24 indices for wireframe
    glBindVertexArray(0);
}

void Renderer::renderSun(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& sunDirection, const glm::vec3& cameraPos) {

    float sunAngle = glm::dot(sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));
    float transitionFactor = (-sunAngle + 1.0f) * 0.5f;
    glm::vec3 sunColour = glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.5f, 0.0f), transitionFactor);

    sunShader->use();

    glUniformMatrix4fv(sunViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(sunProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(sunSunDirLoc, 1, glm::value_ptr(sunDirection));
    glUniform3fv(sunColourLoc, 1, glm::value_ptr(sunColour));
    glUniform3fv(sunCamPosLoc, 1, glm::value_ptr(cameraPos));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // The sun sits at depth 1.0 (the far plane), which only passes where nothing was drawn, so terrain always covers it
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glBindVertexArray(sunVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}
