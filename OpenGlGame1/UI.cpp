#include "UI.h"
#include <algorithm>
#include <cmath>

// Colours used by every widget so the menus look consistent
namespace Style {
	const glm::vec4 text(0.95f, 0.95f, 0.95f, 1.0f);
	const glm::vec4 textDisabled(0.5f, 0.5f, 0.55f, 1.0f);
	const glm::vec4 placeholder(0.55f, 0.55f, 0.6f, 1.0f);
	const glm::vec4 border(0.05f, 0.05f, 0.07f, 0.9f);
	const glm::vec4 button(0.20f, 0.22f, 0.27f, 0.92f);
	const glm::vec4 buttonHover(0.30f, 0.34f, 0.42f, 0.95f);
	const glm::vec4 buttonSelected(0.22f, 0.42f, 0.68f, 0.95f);
	const glm::vec4 buttonDisabled(0.14f, 0.14f, 0.16f, 0.7f);
	const glm::vec4 fieldBackground(0.08f, 0.08f, 0.10f, 0.95f);
	const glm::vec4 fieldFocused(0.12f, 0.12f, 0.16f, 0.95f);
	const glm::vec4 sliderFill(0.22f, 0.42f, 0.68f, 0.95f);
}

UI::UI(UIRenderer& renderer) : renderer(renderer) {
}

void UI::beginFrame(const UIInput& frameInput, int screenWidth, int screenHeight) {
	input = frameInput;
	width = std::max(screenWidth, 1);
	height = std::max(screenHeight, 1);
	// One font pixel = 3 screen pixels at 1080p, 2 at 720p
	pixelScale = std::max(1, static_cast<int>(std::round(height / 360.0f)));
	clickedField = false;

	if (!input.mouseDown) activeSlider = nullptr;//drag ended
	renderer.begin(width, height);
}

void UI::endFrame() {
	// Clicking anywhere that isn't a text field takes the keyboard focus away
	if (input.mousePressed && !clickedField) focusedField = nullptr;
	renderer.flush();
}

void UI::clearFocus() {
	focusedField = nullptr;
	activeSlider = nullptr;
}

bool UI::mouseOver(float x, float y, float w, float h) const {
	return input.mouse.x >= x && input.mouse.x < x + w && input.mouse.y >= y && input.mouse.y < y + h;
}

void UI::panel(float x, float y, float w, float h, const glm::vec4& colour) {
	renderer.rect(x, y, w, h, colour);
}

void UI::label(const std::string& text, float x, float y, int textScale, const glm::vec4& colour) {
	renderer.text(text, x, y, textScale, colour);
}

void UI::labelCentered(const std::string& text, float centreX, float y, int textScale, const glm::vec4& colour) {
	renderer.text(text, centreX - UIRenderer::textWidth(text, textScale) * 0.5f, y, textScale, colour);
}

bool UI::button(const std::string& text, float x, float y, float w, float h, bool enabled, bool selected) {
	bool hovered = enabled && mouseOver(x, y, w, h);
	bool clicked = hovered && input.mousePressed;

	glm::vec4 background = !enabled ? Style::buttonDisabled
		: selected ? Style::buttonSelected
		: hovered ? Style::buttonHover
		: Style::button;

	float border = static_cast<float>(pixelScale);
	renderer.rect(x - border, y - border, w + border * 2, h + border * 2, Style::border);
	renderer.rect(x, y, w, h, background);
	float textY = y + (h - UIRenderer::textHeight(pixelScale)) * 0.5f;
	labelCentered(text, x + w * 0.5f, textY, pixelScale, enabled ? Style::text : Style::textDisabled);
	return clicked;
}

bool UI::slider(const std::string& text, float x, float y, float w, float h, int& value, int min, int max) {
	if (input.mousePressed && mouseOver(x, y, w, h)) activeSlider = &value;

	int oldValue = value;
	if (activeSlider == &value) {
		// Keeps following the mouse even when it leaves the slider, until the button is released
		float t = std::clamp((input.mouse.x - x) / w, 0.0f, 1.0f);
		value = min + static_cast<int>(std::round(t * (max - min)));
	}

	float t = static_cast<float>(value - min) / static_cast<float>(max - min);
	float border = static_cast<float>(pixelScale);
	bool hovered = mouseOver(x, y, w, h) || activeSlider == &value;
	renderer.rect(x - border, y - border, w + border * 2, h + border * 2, Style::border);
	renderer.rect(x, y, w, h, hovered ? Style::buttonHover : Style::button);
	renderer.rect(x, y, w * t, h, Style::sliderFill);

	float textY = y + (h - UIRenderer::textHeight(pixelScale)) * 0.5f;
	labelCentered(text, x + w * 0.5f, textY, pixelScale, Style::text);
	return value != oldValue;
}

bool UI::textField(float x, float y, float w, float h, std::string& text, size_t maxLength,
	bool (*allowed)(char), const std::string& placeholder) {

	if (input.mousePressed && mouseOver(x, y, w, h)) {
		focusedField = &text;
		clickedField = true;
	}
	bool focused = focusedField == &text;

	std::string oldText = text;
	if (focused) {
		for (char c : input.typed) {
			if (text.size() < maxLength && allowed(c)) text.push_back(c);
		}
		for (int i = 0; i < input.backspaces && !text.empty(); i++) text.pop_back();
	}

	float border = static_cast<float>(pixelScale);
	renderer.rect(x - border, y - border, w + border * 2, h + border * 2, Style::border);
	renderer.rect(x, y, w, h, focused ? Style::fieldFocused : Style::fieldBackground);

	float padding = 4.0f * pixelScale;
	float textY = y + (h - UIRenderer::textHeight(pixelScale)) * 0.5f;
	if (text.empty() && !focused) {
		label(placeholder, x + padding, textY, pixelScale, Style::placeholder);
	}
	else {
		label(text, x + padding, textY, pixelScale, Style::text);
	}

	// Blinking cursor after the last character
	if (focused && std::fmod(input.time, 1.0f) < 0.5f) {
		float caretX = x + padding + UIRenderer::textWidth(text, pixelScale) + pixelScale;
		renderer.rect(caretX, textY, static_cast<float>(pixelScale), UIRenderer::textHeight(pixelScale), Style::text);
	}
	return text != oldText;
}
