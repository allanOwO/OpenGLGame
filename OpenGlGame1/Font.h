#pragma once
#include <cstdint>
#include <vector>

// Built in 5x7 pixel font, uppercase only (lowercase letters are drawn as uppercase)
// Glyphs live in a texture of 8x8 pixel cells, one cell per ASCII character from 32 (space) to 127
namespace Font {
	constexpr int glyphWidth = 5;
	constexpr int glyphHeight = 7;
	constexpr int advance = 6;//glyph width + 1 pixel gap

	constexpr int cellSize = 8;
	constexpr int columns = 16;
	constexpr int textureWidth = columns * cellSize;//128
	constexpr int textureHeight = 64;//6 rows of cells, padded to a power of two

	constexpr int firstChar = 32;
	constexpr int whiteCell = 127;//the DEL character's cell is filled solid white, used to draw plain rectangles

	// One byte per pixel (0 = empty, 255 = filled), textureWidth * textureHeight, row 0 at the top
	std::vector<uint8_t> buildTexture();

	// Which cell to use for a character, unknown characters become '?'
	int cellFor(char c);
}
