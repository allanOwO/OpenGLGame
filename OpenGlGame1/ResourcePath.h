#pragma once
#include <string>
#include <filesystem>//to look for resourse file

// Finds a file in ResourceFiles whether the exe runs from Visual Studio or from its build folder
inline std::string getResourcePath(const std::string& subPath) {
	// 1. Check current directory (For the Release .exe)
	if (std::filesystem::exists("ResourceFiles/" + subPath)) {
		return "ResourceFiles/" + subPath;
	}
	// 2. Check one level up (For Visual Studio F5/Debug mode)
	if (std::filesystem::exists("../ResourceFiles/" + subPath)) {
		return "../ResourceFiles/" + subPath;
	}

	// Fallback: return the path as-is
	return "ResourceFiles/" + subPath;
}
