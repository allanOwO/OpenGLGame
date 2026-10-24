#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glad/glad.h>

#include "FileUtil.h"

// Saves what's currently in the default framebuffer as a 24 bit .bmp
// BMP needs no compression library, and like OpenGL it stores rows bottom to top, so they copy straight across
inline bool saveScreenshot(const std::string& path, int width, int height) {
	int rowBytes = (width * 3 + 3) & ~3;//BMP rows are padded to 4 bytes
	std::vector<uint8_t> pixels(static_cast<size_t>(rowBytes) * height);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);//matches the BMP row padding
	glReadBuffer(GL_BACK);//the frame just drawn, not yet swapped to the screen
	glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());

	auto put32 = [](std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (i * 8))); };
	auto put16 = [](std::vector<uint8_t>& out, uint16_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };

	std::vector<uint8_t> file;
	file.reserve(54 + pixels.size());
	// File header
	file.push_back('B'); file.push_back('M');
	put32(file, static_cast<uint32_t>(54 + pixels.size()));
	put32(file, 0);
	put32(file, 54);//pixel data starts after the two headers
	// Info header
	put32(file, 40);
	put32(file, static_cast<uint32_t>(width));
	put32(file, static_cast<uint32_t>(height));//positive = bottom to top
	put16(file, 1);//planes
	put16(file, 24);//bits per pixel
	put32(file, 0);//no compression
	put32(file, static_cast<uint32_t>(pixels.size()));
	put32(file, 2835); put32(file, 2835);//72 dpi
	put32(file, 0); put32(file, 0);
	file.insert(file.end(), pixels.begin(), pixels.end());

	return writeFileAtomically(path, file);
}
