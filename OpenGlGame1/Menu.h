#pragma once
#include <string>
#include <vector>

#include "UI.h"
#include "Settings.h"

enum class MenuScreen {
	None,//menu closed, playing
	Title,
	WorldSelect,
	CreateWorld,
	Settings,
	Pause,
	BenchmarkResults
};

// What the player asked for this frame, Main carries it out
struct MenuAction {
	enum class Type {
		None,
		Quit,
		CreateWorld,//worldName, seedText (empty = random)
		LoadWorld,//worldName
		Resume,
		SaveWorld,
		SaveAndExitToTitle,
		SaveAndQuit,
		SettingsChanged,
		StartBenchmark
	};
	Type type = Type::None;
	std::string worldName;
	std::string seedText;
};

// All the menu screens. Draws the current one and reports clicks as MenuActions,
// it never touches the world or files itself (apart from listing saved worlds)
class Menu
{
public:
	void open(MenuScreen screen);
	void close() { open(MenuScreen::None); }
	bool isOpen() const { return screen != MenuScreen::None; }
	MenuScreen current() const { return screen; }

	void goBack();//Escape key: closes the pause menu, or returns to the previous screen
	void showMessage(const std::string& text, float seconds = 3.0f);//shown under the buttons
	void showBenchmarkResults(const std::vector<std::string>& lines);//opens the results screen

	MenuAction draw(UI& ui, Settings& settings, float time);

	// "123" or "-45", false for anything else; out of range numbers wrap into an int
	static bool parseSeed(const std::string& text, int& seed);

private:
	MenuAction drawTitle(UI& ui);
	MenuAction drawWorldSelect(UI& ui);
	MenuAction drawCreateWorld(UI& ui);
	MenuAction drawSettings(UI& ui, Settings& settings);
	MenuAction drawPause(UI& ui);
	MenuAction drawBenchmarkResults(UI& ui);
	void drawMessage(UI& ui, float y);

	MenuScreen screen = MenuScreen::Title;
	MenuScreen settingsReturnTo = MenuScreen::Title;//settings opens from both the title and pause screens
	bool screenChanged = true;

	std::vector<std::string> worlds;
	int selectedWorld = -1;
	int listScroll = 0;

	std::string newWorldName;
	std::string newWorldSeed;

	std::vector<std::string> benchmarkLines;

	std::string message;
	float messageUntil = 0.0f;
	float now = 0.0f;
};
