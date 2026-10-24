#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "DebugStats.h"

class UI;
class World;
class Renderer;

// Frame times of a stretch of frames boiled down to the usual numbers
struct FrameSummary {
	int frames = 0;
	double averageMs = 0.0;
	double averageFps = 0.0;//frames / total time, not the average of each frame's fps (that overweights fast frames)
	double onePercentLowFps = 0.0;//fps of the 99th percentile frame time, shows stutter the average hides
	double worstMs = 0.0;

	static FrameSummary of(std::vector<float> frameMs);//by value, it gets sorted
};

// Times for one frame, measured in Main
struct FrameTimes {
	float frameMs = 0.0f;//start of one frame to the start of the next
	float updateMs = 0.0f;//CPU: game + world update (chunk loading, job launching, mesh uploads)
	float renderMs = 0.0f;//CPU: issuing the world's draw calls
	float gpuMs = 0.0f;//GPU: actually drawing the world
};

// The F3 screen: frame timing, a frame time graph, position, chunk and job counts, memory
// recordFrame runs every frame even while hidden, so the numbers are ready the moment it's opened
class DebugOverlay
{
public:
	void toggle() { visible = !visible; }
	bool isVisible() const { return visible; }

	void recordFrame(const FrameTimes& times);
	void draw(UI& ui, const World& world, const Renderer& renderer,
		const glm::vec3& cameraPos, const glm::vec3& cameraFront, float yaw, float pitch);

private:
	void updateAverages(const World& world);
	void drawGraph(UI& ui, float right, float bottom, int textScale);

	bool visible = false;

	// Ring buffer of recent frame times for the graph, newest at historyNext - 1
	static constexpr int historySize = 240;
	float history[historySize] = {};
	int historyNext = 0;

	// Numbers are averaged over half a second so they're readable instead of flickering every frame
	static constexpr double averageWindow = 0.5;
	double windowStart = 0.0;
	std::vector<float> windowFrames;
	FrameTimes windowTotals;
	FrameSummary shownFrames;
	FrameTimes shownTimes;//averages
	JobTotals lastGeneration, lastMeshing;
	double shownGenerationMs = 0.0, shownMeshMs = 0.0;
	JobTotals lastEditLatency;
	double shownEditLatencyMs = 0.0;//stays at the last value until another edit lands

	std::string gpuName;//read once, glGetString needs the GL context
};
