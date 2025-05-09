#define TINYGLTF_NO_STB_IMAGE_WRITE 
#define TINYGLTF_IMPLEMENTATION
#include "ModelLoader.h"
#include <iostream>
#include <GLFW/glfw3.h>

std::map<std::string, Model> ModelLoader::modelCache;

Model* ModelLoader::getModel(const std::string& path) {
    auto it = modelCache.find(path); 
    if (it == modelCache.end()) { 
        modelCache[path] = loadGLB(path); 
        it = modelCache.find(path); 
    }
    return &it->second; 
}


Model ModelLoader::loadGLB(const std::string& path) {
    Model model;
    tinygltf::Model gltf;
    tinygltf::TinyGLTF loader;
    std::string error, warn;
    if (!loader.LoadBinaryFromFile(&gltf, &error, &warn, path)) {
        std::cerr << "Failed to load GLB: " << error << std::endl;
        return model;
    }
    if (!warn.empty()) std::cerr << "GLTF Warning: " << warn << std::endl;

    // Check OpenGL context
    if (!glfwGetCurrentContext()) {
        std::cerr << "No current OpenGL context during model loading!" << std::endl;
        return model;
    } 

    const GLubyte* version = glGetString(GL_VERSION); 
    if (version) {
        std::cout << "OpenGL version: " << version << std::endl; 
    }
    else {
        std::cerr << "No valid OpenGL context when loading model!" << std::endl; 
    }



    // Clear any pending errors
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "Pending OpenGL error before VAO creation: 0x" << std::hex << err << std::dec << std::endl;
    }

    auto& mesh = gltf.meshes[0];
    auto& prim = mesh.primitives[0];

    // POSITION
    auto& posA = gltf.accessors[prim.attributes.at("POSITION")];
    auto& posV = gltf.bufferViews[posA.bufferView];
    auto& posB = gltf.buffers[posV.buffer];
    const float* posData = reinterpret_cast<const float*>(posB.data.data() + posV.byteOffset + posA.byteOffset);

    // NORMAL
    const float* normData = nullptr;
    if (prim.attributes.count("NORMAL")) {
        auto& nA = gltf.accessors[prim.attributes.at("NORMAL")];
        auto& nV = gltf.bufferViews[nA.bufferView];
        auto& nB = gltf.buffers[nV.buffer];
        normData = reinterpret_cast<const float*>(nB.data.data() + nV.byteOffset + nA.byteOffset);
    }

    // TEXCOORD_0
    const float* uvData = nullptr;
    if (prim.attributes.count("TEXCOORD_0")) {
        auto& tA = gltf.accessors[prim.attributes.at("TEXCOORD_0")];
        auto& tV = gltf.bufferViews[tA.bufferView];
        auto& tB = gltf.buffers[tV.buffer];
        uvData = reinterpret_cast<const float*>(tB.data.data() + tV.byteOffset + tA.byteOffset);
    }

    // Build vertices
    for (size_t i = 0; i < posA.count; ++i) {
        Vertex v;
        v.position = glm::vec3(
            posData[3 * i + 0],
            posData[3 * i + 1],
            posData[3 * i + 2]
        );
        v.normal = normData
            ? glm::vec3(normData[3 * i + 0], normData[3 * i + 1], normData[3 * i + 2])
            : glm::vec3(0, 1, 0);
        v.texCoord = uvData
            ? glm::vec2(uvData[2 * i + 0], uvData[2 * i + 1])
            : glm::vec2(0);
        model.vertices.push_back(v);
    }

    // Indices
    auto& idxA = gltf.accessors[prim.indices];
    auto& idxV = gltf.bufferViews[idxA.bufferView];
    auto& idxB = gltf.buffers[idxV.buffer];
    const void* idxData = idxB.data.data() + idxV.byteOffset + idxA.byteOffset;
    model.indices.resize(idxA.count);
    if (idxA.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        auto ptr = reinterpret_cast<const unsigned short*>(idxData);
        for (int i = 0; i < idxA.count; ++i) model.indices[i] = ptr[i];
    }
    else {
        auto ptr = reinterpret_cast<const unsigned int*>(idxData);
        for (int i = 0; i < idxA.count; ++i) model.indices[i] = ptr[i];
    }

    err = glGetError(); 
    if (err != GL_NO_ERROR) {
        std::cerr << "OpenGL error before VAO creation: 0x" << std::hex << err << std::dec << std::endl;
    }

    glGenVertexArrays(1, &model.VAO);
    checkGLError("glGenVertexArrays");

    std::cout << "Generated VAO: " << model.VAO << std::endl;
    if (!glIsVertexArray(model.VAO))
        std::cerr << "VAO creation failed immediately!" << std::endl;


    glGenBuffers(1, &model.VBO);
    checkGLError("glGenBuffers - VBO");

    glGenBuffers(1, &model.EBO);
    checkGLError("glGenBuffers - EBO");

    glBindVertexArray(model.VAO);
    checkGLError("glBindVertexArray");

    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glBufferData(GL_ARRAY_BUFFER,
        model.vertices.size() * sizeof(Vertex),
        model.vertices.data(), GL_STATIC_DRAW);
    checkGLError("VBO glBufferData");

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        model.indices.size() * sizeof(unsigned int),  // might change to `unsigned short`
        model.indices.data(), GL_STATIC_DRAW);
    checkGLError("EBO glBufferData");

    // Attributes
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position)); 
    glEnableVertexAttribArray(0); 
    checkGLError("Attrib 0 - position"); 

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord)); 
    glEnableVertexAttribArray(1);
    checkGLError("Attrib 1 - texCoord");

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    checkGLError("Attrib 2 - normal");

    glBindVertexArray(0);
    checkGLError("glBindVertexArray(0) - unbind");


    // Load texture if available
    if (prim.material >= 0) {
        auto& material = gltf.materials[prim.material];
        if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            int texIndex = material.pbrMetallicRoughness.baseColorTexture.index;
            model.textureID = loadTexture(gltf, texIndex);
        }
        else {
            model.textureID = loadFallbackTexture(); // No texture; shader will handle it
        }
    }
    else {
        model.textureID = loadFallbackTexture(); // No material; shader will handle it 
    }


    return model;

}

unsigned int ModelLoader::loadTexture(const tinygltf::Model& model, int textureIndex) {
    if (!glfwGetCurrentContext()) {
        std::cerr << "No current OpenGL context during texture loading!" << std::endl;
        return loadFallbackTexture();
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);
    checkGLError("glGenTextures");
    if (!glIsTexture(textureID)) {
        std::cerr << "Generated texture ID " << textureID << " is invalid immediately!" << std::endl;
        return loadFallbackTexture();
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    checkGLError("glBindTexture - loadTexture");

    const auto& image = model.images[model.textures[textureIndex].source];
    if (image.image.empty()) {
        std::cerr << "Texture image data is empty, using fallback" << std::endl;
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &textureID);
        return loadFallbackTexture();
    }

    GLenum format = (image.component == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.image.data());
    checkGLError("glTexImage2D");
    glGenerateMipmap(GL_TEXTURE_2D);
    checkGLError("glGenerateMipmap");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGLError("glTexParameteri");

    std::cout << "Loaded texture ID: " << textureID << ", width: " << image.width << ", height: " << image.height << std::endl;
    std::cout << "Texture ID: " << textureID << ", IsTexture: " << glIsTexture(textureID) << std::endl;

    glBindTexture(GL_TEXTURE_2D, 0);
    return textureID;
}

unsigned int ModelLoader::loadFallbackTexture() {
    if (!glfwGetCurrentContext()) {
        std::cerr << "No current OpenGL context during fallback texture loading!" << std::endl;
        return 0; // Return 0 to indicate failure
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);
    checkGLError("glGenTextures - fallback");
    if (!glIsTexture(textureID)) {
        std::cerr << "Fallback texture ID " << textureID << " is invalid!" << std::endl;
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    checkGLError("glBindTexture - fallback");

    unsigned char data[] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    checkGLError("glTexImage2D - fallback"); 

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGLError("glTexParameteri - fallback");

    glBindTexture(GL_TEXTURE_2D, 0);
    std::cout << "Loaded fallback texture ID: " << textureID << std::endl;
    return textureID;
}

void ModelLoader::checkGLError(const std::string& stage) { 
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL error at " << stage << ": 0x"
            << std::hex << err << std::dec << std::endl;
    }
} 


