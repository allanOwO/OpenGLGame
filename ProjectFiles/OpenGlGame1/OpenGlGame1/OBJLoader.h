#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>

struct OBJData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> indices;
};

class OBJLoader
{
public:
    static OBJData LoadOBJ(const std::string& filePath); 

private:

};

