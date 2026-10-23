#pragma once
#include <string>
#include <glm/glm.hpp>

#include "UIRenderer.h"

// Mouse and keyboard input for one frame of UI, filled in from the GLFW callbacks
struct UIInput {
	glm::vec2 mouse = glm::vec2(0.0f);//pixels from the top left
	bool mouseDown = false;
	bool mousePressed = false;//went down since last frame
	std::string typed;//characters typed since last frame
	int backspaces = 0;
	float scroll = 0.0f;//mouse wheel, positive is up
	float time = 0.0f;//seconds, for the blinking text cursor
};

// Immediate mode UI: widgets are function calls made every frame that draw themselves and return what happened
//   if (ui.button("PLAY", x, y, w, h)) { ...clicked... }
// There are no widget objects to create or keep in sync, the menu code is just ordinary if statements
class UI
{
public:
	explicit UI(UIRenderer& renderer);

	void beginFrame(const UIInput& input, int screenWidth, int screenHeight);
	void endFrame();//draws everything
	void clearFocus();//call when changing screen so a hidden text field doesn't keep the keyboard

	int scale() const { return pixelScale; }//size of one font pixel, grows with the window
	int screenWidth() const { return width; }
	int screenHeight() const { return height; }
	float scroll() const { return input.scroll; }

	void panel(float x, float y, float w, float h, const glm::vec4& colour);
	void label(const std::string& text, float x, float y, int textScale, const glm::vec4& colour);
	void labelCentered(const std::string& text, float centreX, float y, int textScale, const glm::vec4& colour);

	// true on the frame it is clicked
	bool button(const std::string& text, float x, float y, float w, float h, bool enabled = true, bool selected = false);

	// Drag to change value between min and max, true while the value changes
	bool slider(const std::string& text, float x, float y, float w, float h, int& value, int min, int max);

	// Click to focus, then type, allowed decides which characters are accepted
	bool textField(float x, float y, float w, float h, std::string& text, size_t maxLength,
		bool (*allowed)(char), const std::string& placeholder);

private:
	bool mouseOver(float x, float y, float w, float h) const;

	UIRenderer& renderer;
	UIInput input;
	int width = 1, height = 1;
	int pixelScale = 2;

	// Interactions that last more than one frame, identified by the address of the value they edit
	const void* activeSlider = nullptr;
	const void* focusedField = nullptr;
	bool clickedField = false;//a text field took this frame's click
};
