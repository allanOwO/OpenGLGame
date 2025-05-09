#include "OBJLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>

struct VertexKey {
    int pos, uv, norm;
    bool operator==(const VertexKey& other) const {
        return pos == other.pos && uv == other.uv && norm == other.norm;
    }
};

namespace std {
    template <>
    struct hash<VertexKey> {
        size_t operator()(const VertexKey& k) const {
            return ((hash<int>()(k.pos) ^ (hash<int>()(k.uv) << 1)) >> 1) ^ (hash<int>()(k.norm) << 1);
        }
    };
}

OBJData OBJLoader::LoadOBJ(const std::string& filePath) {
    std::vector<glm::vec3> tempPos;
    std::vector<glm::vec2> tempUV;
    std::vector<glm::vec3> tempNorm;

    std::unordered_map<VertexKey, unsigned int> vertexMap;
    OBJData out;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ: " << filePath << std::endl;
        return out;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            tempPos.push_back(pos);
        }
        else if (prefix == "vt") {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            tempUV.push_back(uv);
        }
        else if (prefix == "vn") {
            glm::vec3 norm; 
            ss >> norm.x >> norm.y >> norm.z;
            tempNorm.push_back(norm);
        }
        else if (prefix == "f") {
            for (int i = 0; i < 3; ++i) {
                std::string vtx;
                ss >> vtx;
                std::replace(vtx.begin(), vtx.end(), '/', ' ');
                std::istringstream vss(vtx);
                VertexKey key = { -1, -1, -1 };
                vss >> key.pos >> key.uv >> key.norm;

                key.pos--; key.uv--; key.norm--; // OBJ is 1-based

                if (vertexMap.find(key) == vertexMap.end()) {
                    vertexMap[key] = static_cast<unsigned int>(out.positions.size());
                    out.positions.push_back(tempPos[key.pos]);
                    if (key.uv >= 0) {
                        glm::vec2 flippedUV = tempUV[key.uv];
                        flippedUV.y = 1.0f - flippedUV.y;
                        out.texCoords.push_back(tempUV[key.uv]);
                    } 
                    if (key.norm >= 0) out.normals.push_back(tempNorm[key.norm]);
                }

                out.indices.push_back(vertexMap[key]);
            }
        }
    }

    return out;
}
