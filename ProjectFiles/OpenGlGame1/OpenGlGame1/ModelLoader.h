#pragma once
#include <tiny_gltf.h>
#include <string>
#include "Model.h"

class ModelLoader
{
public:

	static Model* getModel(const std::string& path);
	static unsigned int loadFallbackTexture();

private:
	static unsigned int loadTexture(const tinygltf::Model& model, int textureIndex);
	
	static Model loadGLB(const std::string& path);
	static std::map<std::string, Model> modelCache; 
	static void checkGLError(const std::string& stage);
};

