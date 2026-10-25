#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "DebugStats.h"
#include "DebugOverlay.h"

class World;

// A repeatable performance test: same seed, same camera path, same sun position every run
// so numbers from different versions of the engine can be compared directly
//
// Phases:
//   LOAD   - camera waits at spawn until everything in render distance is generated and meshed
//   SPIN   - turns on the spot for a full circle, nothing new loads, so this is pure rendering cost
//   FLIGHT - flies in a straight line, chunks stream in and out the whole time
//   EDITS  - places then removes blocks at a fixed rate, measures how long an edit takes to show up
//   ZOOM   - zooms in and out (4x, 8x, 16x, off, 4x) while panning, like pressing C and scrolling
//            last so the phases before it stay comparable with older runs (it's in the report, not history.csv)
//
// Results are written to benchmarks/<date>.txt and a line is added to benchmarks/history.csv
class Benchmark
{
public:
	static constexpr int seed = 1337;
	static constexpr float sunTime = 12.0f;//fixed sun position (Renderer's time), high enough for shadows

	// world should be brand new, made with Benchmark::seed
	Benchmark(const World& world, int screenWidth, int screenHeight);

	// Call once per frame before World::update with the length of the frame that just finished
	// times/render describe that same frame
	void recordFrame(const FrameTimes& times, const RenderStats& render, const UpdateTimes& parts);
	void update(World& world, double deltaSeconds);

	bool isFinished() const { return phase == Phase::Done; }

	// Camera for this frame
	glm::vec3 cameraPos() const { return position; }
	glm::vec3 cameraFront() const;
	glm::mat4 viewMatrix() const;
	float getYaw() const { return yaw; }
	float getPitch() const { return pitch; }
	float zoom() const { return zoomLevel; }//1 except in the zoom phase

	std::string status() const;//"FLIGHT 12.0 / 20.0 S", shown while it runs

	// A file name when this frame should be saved as a screenshot (call after the world is drawn, before the HUD)
	// Same camera spots every run, so screenshots from different versions can be compared side by side
	std::string screenshotPath();

	// Call once finished, writes the result files and returns the summary to show on screen
	std::vector<std::string> finish();

private:
	enum class Phase { Load, Spin, Flight, Edits, Zoom, Done };

	// Everything measured during one phase
	struct PhaseRecord {
		std::vector<float> frameMs;
		std::vector<float> updateList, renderList, gpuList;//every frame's parts, for the slowest 1% table
		std::vector<UpdateTimes> updateParts;
		double updateMs = 0.0, renderMs = 0.0, gpuMs = 0.0, gpuShadowMs = 0.0;
		int64_t triangles = 0, drawCalls = 0;
		double seconds = 0.0;
		JobTotals generationStart, meshStart, editStart;//world timers when the phase began
		JobTotals generationEnd, meshEnd, editEnd;

		FrameSummary frames() const { return FrameSummary::of(frameMs); }
		double average(double total) const { return frameMs.empty() ? 0.0 : total / frameMs.size(); }
	};

	void startPhase(Phase next, const World& world);
	void endPhase(const World& world);
	void placeCamera();//from the phase and how far into it we are
	void doEdits(World& world);
	float pathHeight(float x) const;//camera height along the flight path

	PhaseRecord& record() { return records[static_cast<int>(phase)]; }

	static const char* phaseName(Phase p);

	Phase phase = Phase::Load;
	double phaseTime = 0.0;//seconds into the current phase
	PhaseRecord records[5];
	bool loadTimedOut = false;
	int chunksAfterLoad = 0;
	int editsDone = 0;

	glm::vec3 position = glm::vec3(0.0f);
	float yaw = 0.0f, pitch = 0.0f;
	float zoomLevel = 1.0f;
	glm::ivec3 editOrigin = glm::ivec3(0);

	// Flight height samples every pathStep blocks along +X, smoothed so the camera glides over hills
	std::vector<float> heights;

	int renderDistance;
	std::string lodText;//where each LOD level started, LOD changes the results so the report says what it was
	int screenWidth, screenHeight;
	std::string runName;//benchmark_<start time>, shared by the report and screenshots
	int screenshotsTaken = 0;
	WorldStats finalStats;
	RenderStats lastRender;
};
