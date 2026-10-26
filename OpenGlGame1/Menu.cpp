#include "Menu.h"
#include <algorithm>
#include <cctype>
#include <cstdint>

#include "WorldSave.h"

namespace {
	const glm::vec4 white(0.95f, 0.95f, 0.95f, 1.0f);
	const glm::vec4 grey(0.65f, 0.67f, 0.72f, 1.0f);
	const glm::vec4 messageColour(1.0f, 0.85f, 0.4f, 1.0f);
	const glm::vec4 dim(0.0f, 0.0f, 0.0f, 0.5f);
	const glm::vec4 listBackground(0.0f, 0.0f, 0.0f, 0.35f);

	bool isSeedCharacter(char c) {
		return std::isdigit(static_cast<unsigned char>(c)) || c == '-';
	}

	// Sizes are in font pixels (ui.scale()) so the menus grow with the window
	struct Layout {
		float s, centreX, buttonW, buttonH, gap;
		explicit Layout(const UI& ui)
			: s(static_cast<float>(ui.scale())),
			centreX(ui.screenWidth() * 0.5f),
			buttonW(120.0f * s), buttonH(14.0f * s), gap(5.0f * s) {}
		float left() const { return centreX - buttonW * 0.5f; }
	};
}

void Menu::open(MenuScreen newScreen) {
	if (newScreen == MenuScreen::WorldSelect) {
		worlds = WorldSave::listWorlds();
		selectedWorld = worlds.empty() ? -1 : 0;
		listScroll = 0;
	}
	if (newScreen == MenuScreen::CreateWorld) {
		newWorldName.clear();
		newWorldSeed.clear();
	}
	screen = newScreen;
	screenChanged = true;
	messageUntil = 0.0f;//messages belong to the screen they were shown on
}

void Menu::goBack() {
	switch (screen) {
	case MenuScreen::WorldSelect: open(MenuScreen::Title); break;
	case MenuScreen::CreateWorld: open(MenuScreen::WorldSelect); break;
	case MenuScreen::Settings: open(settingsReturnTo); break;
	case MenuScreen::Pause: close(); break;
	case MenuScreen::BenchmarkResults: open(MenuScreen::Title); break;
	default: break;//nothing behind the title screen
	}
}

void Menu::showMessage(const std::string& text, float seconds) {
	message = text;
	messageUntil = now + seconds;
}

void Menu::showBenchmarkResults(const std::vector<std::string>& lines) {
	benchmarkLines = lines;
	open(MenuScreen::BenchmarkResults);
}

bool Menu::parseSeed(const std::string& text, int& seed) {
	size_t digitsStart = (!text.empty() && text[0] == '-') ? 1 : 0;
	if (text.size() == digitsStart) return false;//empty or just "-"
	for (size_t i = digitsStart; i < text.size(); i++) {
		if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
	}
	try {
		long long value = std::stoll(text);
		seed = static_cast<int>(static_cast<uint32_t>(value));//wrap big numbers instead of rejecting them
		return true;
	}
	catch (const std::exception&) {
		return false;//longer than a long long
	}
}

MenuAction Menu::draw(UI& ui, Settings& settings, float time) {
	now = time;
	if (screenChanged) {
		ui.clearFocus();
		screenChanged = false;
	}

	switch (screen) {
	case MenuScreen::Title: return drawTitle(ui);
	case MenuScreen::WorldSelect: return drawWorldSelect(ui);
	case MenuScreen::CreateWorld: return drawCreateWorld(ui);
	case MenuScreen::Settings: return drawSettings(ui, settings);
	case MenuScreen::Pause: return drawPause(ui);
	case MenuScreen::BenchmarkResults: return drawBenchmarkResults(ui);
	default: return {};
	}
}

void Menu::drawMessage(UI& ui, float y) {
	if (now < messageUntil) {
		ui.labelCentered(message, ui.screenWidth() * 0.5f, y, ui.scale(), messageColour);
	}
}

// ---------- screens ----------

MenuAction Menu::drawTitle(UI& ui) {
	Layout l(ui);
	MenuAction action;

	float y = ui.screenHeight() * 0.22f;
	ui.labelCentered("OPENGL GAME", l.centreX, y, ui.scale() * 4, white);
	y += UIRenderer::textHeight(ui.scale() * 4) + l.gap * 6;

	if (ui.button("PLAY", l.left(), y, l.buttonW, l.buttonH)) open(MenuScreen::WorldSelect);
	y += l.buttonH + l.gap;
	if (ui.button("SETTINGS", l.left(), y, l.buttonW, l.buttonH)) {
		settingsReturnTo = MenuScreen::Title;
		open(MenuScreen::Settings);
	}
	y += l.buttonH + l.gap;
	if (ui.button("BENCHMARK", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::StartBenchmark;
	y += l.buttonH + l.gap;
	if (ui.button("QUIT", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::Quit;
	y += l.buttonH + l.gap * 2;

	drawMessage(ui, y);
	return action;
}

MenuAction Menu::drawWorldSelect(UI& ui) {
	Layout l(ui);
	MenuAction action;

	float y = ui.screenHeight() * 0.12f;
	ui.labelCentered("SELECT WORLD", l.centreX, y, ui.scale() * 2, white);
	y += UIRenderer::textHeight(ui.scale() * 2) + l.gap * 3;

	// Scrollable list, only visibleRows are drawn at a time
	const int visibleRows = 6;
	float listW = l.buttonW * 1.4f;
	float listX = l.centreX - listW * 0.5f;
	float rowH = l.buttonH;
	float listH = visibleRows * (rowH + l.gap) + l.gap;
	ui.panel(listX - l.gap, y, listW + l.gap * 2, listH, listBackground);

	int maxScroll = std::max(0, static_cast<int>(worlds.size()) - visibleRows);
	// The list is the only thing on this screen that scrolls, so the wheel works anywhere
	if (ui.scroll() != 0.0f) {
		listScroll = std::clamp(listScroll - static_cast<int>(ui.scroll()), 0, maxScroll);
	}

	if (worlds.empty()) {
		ui.labelCentered("NO SAVED WORLDS YET", l.centreX, y + listH * 0.5f - UIRenderer::textHeight(ui.scale()) * 0.5f, ui.scale(), grey);
	}
	float rowY = y + l.gap;
	for (int i = listScroll; i < static_cast<int>(worlds.size()) && i < listScroll + visibleRows; i++) {
		if (ui.button(worlds[i], listX, rowY, listW, rowH, true, i == selectedWorld)) selectedWorld = i;
		rowY += rowH + l.gap;
	}
	if (maxScroll > 0) {
		std::string range = std::to_string(listScroll + 1) + "-" + std::to_string(std::min(listScroll + visibleRows, static_cast<int>(worlds.size())))
			+ " OF " + std::to_string(worlds.size()) + "  (SCROLL FOR MORE)";
		ui.labelCentered(range, l.centreX, y + listH + l.gap, ui.scale(), grey);
	}
	y += listH + l.gap * 5;

	bool hasSelection = selectedWorld >= 0 && selectedWorld < static_cast<int>(worlds.size());
	if (ui.button("PLAY SELECTED WORLD", l.left(), y, l.buttonW, l.buttonH, hasSelection)) {
		action.type = MenuAction::Type::LoadWorld;
		action.worldName = worlds[selectedWorld];
	}
	y += l.buttonH + l.gap;
	if (ui.button("CREATE NEW WORLD", l.left(), y, l.buttonW, l.buttonH)) open(MenuScreen::CreateWorld);
	y += l.buttonH + l.gap;
	if (ui.button("BACK", l.left(), y, l.buttonW, l.buttonH)) open(MenuScreen::Title);
	y += l.buttonH + l.gap * 2;

	drawMessage(ui, y);
	return action;
}

MenuAction Menu::drawCreateWorld(UI& ui) {
	Layout l(ui);
	MenuAction action;

	float y = ui.screenHeight() * 0.18f;
	ui.labelCentered("CREATE WORLD", l.centreX, y, ui.scale() * 2, white);
	y += UIRenderer::textHeight(ui.scale() * 2) + l.gap * 4;

	ui.label("WORLD NAME", l.left(), y, ui.scale(), grey);
	y += UIRenderer::textHeight(ui.scale()) + l.gap;
	ui.textField(l.left(), y, l.buttonW, l.buttonH, newWorldName, WorldSave::maxNameLength, WorldSave::isNameCharacter, "NEW WORLD");
	y += l.buttonH + l.gap * 3;

	ui.label("SEED (BLANK FOR RANDOM)", l.left(), y, ui.scale(), grey);
	y += UIRenderer::textHeight(ui.scale()) + l.gap;
	ui.textField(l.left(), y, l.buttonW, l.buttonH, newWorldSeed, 11, isSeedCharacter, "RANDOM");
	y += l.buttonH + l.gap * 4;

	if (ui.button("CREATE", l.left(), y, l.buttonW, l.buttonH)) {
		// Check everything here so Main only ever gets a request it can carry out
		std::string name = newWorldName.empty() ? "New World" : newWorldName;
		int unusedSeed;
		if (!WorldSave::isValidName(name)) {
			showMessage("NAME CAN'T START OR END WITH A SPACE");
		}
		else if (WorldSave::exists(name)) {
			showMessage("A WORLD WITH THAT NAME ALREADY EXISTS");
		}
		else if (!newWorldSeed.empty() && !parseSeed(newWorldSeed, unusedSeed)) {
			showMessage("SEED MUST BE A WHOLE NUMBER");
		}
		else {
			action.type = MenuAction::Type::CreateWorld;
			action.worldName = name;
			action.seedText = newWorldSeed;
		}
	}
	y += l.buttonH + l.gap;
	if (ui.button("BACK", l.left(), y, l.buttonW, l.buttonH)) open(MenuScreen::WorldSelect);
	y += l.buttonH + l.gap * 2;

	drawMessage(ui, y);
	return action;
}

MenuAction Menu::drawSettings(UI& ui, Settings& settings) {
	Layout l(ui);
	MenuAction action;
	if (settingsReturnTo == MenuScreen::Pause) ui.panel(0, 0, static_cast<float>(ui.screenWidth()), static_cast<float>(ui.screenHeight()), dim);

	float y = ui.screenHeight() * 0.18f;
	ui.labelCentered("SETTINGS", l.centreX, y, ui.scale() * 2, white);
	y += UIRenderer::textHeight(ui.scale() * 2) + l.gap * 4;

	// Graphics, new options go in this section
	ui.label("GRAPHICS", l.left(), y, ui.scale(), grey);
	y += UIRenderer::textHeight(ui.scale()) + l.gap * 2;

	std::string distanceText = "RENDER DISTANCE: " + std::to_string(settings.renderDistance) + " CHUNKS";
	if (ui.slider(distanceText, l.left(), y, l.buttonW, l.buttonH, settings.renderDistance,
		Settings::minRenderDistance, Settings::maxRenderDistance)) {
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap;

	std::string detailText = "DETAIL DISTANCE: " + std::to_string(settings.detailDistance) + " CHUNKS"
		+ (settings.detailDistance < settings.renderDistance ? " (FAR TERRAIN PAST IT)" : "");
	if (ui.slider(detailText, l.left(), y, l.buttonW, l.buttonH, settings.detailDistance,
		Settings::minRenderDistance, Settings::maxRenderDistance)) {
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap;

	std::string simulationText = "SIMULATION DISTANCE: " + std::to_string(settings.simulationDistance) + " CHUNKS";
	if (ui.slider(simulationText, l.left(), y, l.buttonW, l.buttonH, settings.simulationDistance,
		Settings::minSimulationDistance, Settings::maxRenderDistance)) {
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap * 3;

	// Level of detail, each slider is where that level starts as a percentage of how far real chunks go
	// (the detail distance, or render distance if that's shorter)
	// Dragging one past another pushes it along, so they always stay 2x -> 4x -> 8x going outwards
	ui.label("LEVEL OF DETAIL (FAR TERRAIN IN BIGGER BLOCKS)", l.left(), y, ui.scale(), grey);
	y += UIRenderer::textHeight(ui.scale()) + l.gap * 2;
	auto lodText = [&](const std::string& name, int percent) {
		if (percent >= 100) return name + " BLOCKS: OFF";
		int chunks = static_cast<int>(percent * 0.01f * std::min(settings.renderDistance, settings.detailDistance) + 0.5f);
		return name + " BLOCKS FROM " + std::to_string(percent) + "% (" + std::to_string(chunks) + " CHUNKS)";
	};
	if (ui.slider(lodText("2X", settings.lod2xPercent), l.left(), y, l.buttonW, l.buttonH, settings.lod2xPercent, 1, 100)) {
		settings.lod4xPercent = std::max(settings.lod4xPercent, settings.lod2xPercent);
		settings.lod8xPercent = std::max(settings.lod8xPercent, settings.lod4xPercent);
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap;
	if (ui.slider(lodText("4X", settings.lod4xPercent), l.left(), y, l.buttonW, l.buttonH, settings.lod4xPercent, 1, 100)) {
		settings.lod2xPercent = std::min(settings.lod2xPercent, settings.lod4xPercent);
		settings.lod8xPercent = std::max(settings.lod8xPercent, settings.lod4xPercent);
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap;
	if (ui.slider(lodText("8X", settings.lod8xPercent), l.left(), y, l.buttonW, l.buttonH, settings.lod8xPercent, 1, 100)) {
		settings.lod4xPercent = std::min(settings.lod4xPercent, settings.lod8xPercent);
		settings.lod2xPercent = std::min(settings.lod2xPercent, settings.lod4xPercent);
		action.type = MenuAction::Type::SettingsChanged;
	}
	y += l.buttonH + l.gap * 6;

	if (ui.button("BACK", l.left(), y, l.buttonW, l.buttonH)) open(settingsReturnTo);
	return action;
}

MenuAction Menu::drawPause(UI& ui) {
	Layout l(ui);
	MenuAction action;
	ui.panel(0, 0, static_cast<float>(ui.screenWidth()), static_cast<float>(ui.screenHeight()), dim);//darken the game behind

	float y = ui.screenHeight() * 0.2f;
	ui.labelCentered("PAUSED", l.centreX, y, ui.scale() * 3, white);
	y += UIRenderer::textHeight(ui.scale() * 3) + l.gap * 5;

	if (ui.button("RESUME", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::Resume;
	y += l.buttonH + l.gap;
	if (ui.button("SETTINGS", l.left(), y, l.buttonW, l.buttonH)) {
		settingsReturnTo = MenuScreen::Pause;
		open(MenuScreen::Settings);
	}
	y += l.buttonH + l.gap;
	if (ui.button("SAVE GAME", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::SaveWorld;
	y += l.buttonH + l.gap;
	if (ui.button("SAVE AND QUIT TO TITLE", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::SaveAndExitToTitle;
	y += l.buttonH + l.gap;
	if (ui.button("SAVE AND QUIT GAME", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::SaveAndQuit;
	y += l.buttonH + l.gap * 2;

	drawMessage(ui, y);
	return action;
}

MenuAction Menu::drawBenchmarkResults(UI& ui) {
	Layout l(ui);
	MenuAction action;

	float y = ui.screenHeight() * 0.12f;
	ui.labelCentered("BENCHMARK RESULTS", l.centreX, y, ui.scale() * 2, white);
	y += UIRenderer::textHeight(ui.scale() * 2) + l.gap * 4;

	// Smaller text, the lines are long and left aligned so the columns of numbers line up
	int textScale = std::max(1, ui.scale() - 1);
	float widest = 0.0f;
	for (const std::string& line : benchmarkLines) widest = std::max(widest, UIRenderer::textWidth(line, textScale));
	float x = std::max(l.gap, l.centreX - widest * 0.5f);
	float lineH = UIRenderer::textHeight(textScale) + 4.0f * textScale;
	ui.panel(x - l.gap, y - l.gap, widest + l.gap * 2, lineH * benchmarkLines.size() + l.gap * 2, listBackground);
	for (const std::string& line : benchmarkLines) {
		ui.label(line, x, y, textScale, white);
		y += lineH;
	}
	y += l.gap * 4;

	if (ui.button("RUN AGAIN", l.left(), y, l.buttonW, l.buttonH)) action.type = MenuAction::Type::StartBenchmark;
	y += l.buttonH + l.gap;
	if (ui.button("BACK", l.left(), y, l.buttonW, l.buttonH)) open(MenuScreen::Title);
	return action;
}
