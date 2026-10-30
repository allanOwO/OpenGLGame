#include "Benchmark.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "World.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX//windows.h would otherwise turn min and max into macros
#include <windows.h>
#include <psapi.h>

namespace {
	// Changing any of these changes the test, so old results stop being comparable
	const double loadTimeout = 180.0;//give up waiting for the world after this long
	const double spinSeconds = 10.0;
	const double flightSeconds = 20.0;
	const float flightSpeed = 32.0f;//blocks per second, 2 chunks a second
	const double editInterval = 0.05;//20 edits a second
	const int editCount = 100;
	const double editSeconds = editCount * editInterval + 0.5;//a little extra so the last meshes land
	const double zoomSeconds = 12.0;
	const float zoomPanSpeed = 20.0f;//degrees per second, turning while zoomed
	const double vistaRiseSeconds = 4.0;
	const double vistaPanSeconds = 20.0;//one full circle once it's up there
	const double vistaSeconds = vistaRiseSeconds + vistaPanSeconds;
	const float vistaHeight = 500.0f;//world y, the tallest peaks are under 200
	const float vistaPitch = -18.0f;//degrees, low enough to see the ground under the camera and the far edge
	const float pathStep = 4.0f;//blocks between height samples
	const float clearance = 20.0f;//camera height above the smoothed terrain

	const char* resultsFolder = "benchmarks";

	std::string fixed(double value, int decimals) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
		return buffer;
	}

	std::string megabytes(size_t bytes) {
		return fixed(bytes / (1024.0 * 1024.0), 1);
	}

	// The value 99% of frames are faster than, by value since nth_element reorders it
	double percentile99(std::vector<float> values) {
		if (values.empty()) return 0.0;
		size_t index = std::min(values.size() - 1, static_cast<size_t>(values.size() * 0.99));
		std::nth_element(values.begin(), values.begin() + index, values.end());
		return values[index];
	}

	// RAM the whole game uses, and the most it has used at once
	void processMemory(size_t& now, size_t& peak) {
		PROCESS_MEMORY_COUNTERS counters{};
		now = peak = 0;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
			now = counters.WorkingSetSize;
			peak = counters.PeakWorkingSetSize;
		}
	}

	std::string timestamp(const char* format) {
		std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		char buffer[64];
		std::strftime(buffer, sizeof(buffer), format, &local);
		return buffer;
	}
}

Benchmark::Benchmark(const World& world, int screenWidth, int screenHeight)
	: renderDistance(world.getFarDistance()),
	lodText("DETAIL " + std::to_string(world.getRenderDistance()) + "  LOD " + std::to_string(world.getLodPercent(0)) + "/" + std::to_string(world.getLodPercent(1)) + "/" + std::to_string(world.getLodPercent(2)) + "%  SIMULATION " + std::to_string(world.getSimulationDistance())),
	screenWidth(screenWidth), screenHeight(screenHeight),
	runName("benchmark_" + timestamp("%Y-%m-%d_%H-%M-%S")) {

	// Sample the terrain along the flight path once, so the path is the same every run
	const TerrainGenerator& terrain = world.getTerrain();
	float pathLength = flightSpeed * static_cast<float>(flightSeconds) + 64.0f;
	int samples = static_cast<int>(pathLength / pathStep) + 1;
	std::vector<float> raw(samples);
	for (int i = 0; i < samples; i++) {
		// Highest point of a small square around the path, so the camera clears hills just off to the side too
		float x = i * pathStep;
		float highest = 0.0f;
		for (float dx = -8.0f; dx <= 8.0f; dx += 8.0f) {
			for (float dz = -8.0f; dz <= 8.0f; dz += 8.0f) {
				highest = std::max(highest, terrain.getHeight(x + dx, dz));
			}
		}
		highest = std::max(highest, static_cast<float>(Chunk::seaLevel - 1));//the camera stays above the water too
		raw[i] = highest - Chunk::baseTerrainHeight;//chunk local y to world y
	}

	// Look ahead (running max) so the camera starts climbing before a hill, then blur so it doesn't jerk
	const int window = 8;
	std::vector<float> ahead(samples);
	for (int i = 0; i < samples; i++) {
		float highest = raw[i];
		for (int j = std::max(0, i - window); j <= std::min(samples - 1, i + window); j++) highest = std::max(highest, raw[j]);
		ahead[i] = highest;
	}
	heights.resize(samples);
	for (int i = 0; i < samples; i++) {
		float total = 0.0f;
		int count = 0;
		for (int j = std::max(0, i - window); j <= std::min(samples - 1, i + window); j++) {
			total += ahead[j];
			count++;
		}
		heights[i] = total / count;
	}

	startPhase(Phase::Load, world);
	placeCamera();
}

float Benchmark::pathHeight(float x) const {
	float index = std::clamp(x / pathStep, 0.0f, static_cast<float>(heights.size() - 1));
	int below = static_cast<int>(index);
	int above = std::min(below + 1, static_cast<int>(heights.size()) - 1);
	return glm::mix(heights[below], heights[above], index - below) + clearance;
}

glm::vec3 Benchmark::cameraFront() const {
	return glm::normalize(glm::vec3(
		cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
		sin(glm::radians(pitch)),
		sin(glm::radians(yaw)) * cos(glm::radians(pitch))));
}

glm::mat4 Benchmark::viewMatrix() const {
	return glm::lookAt(position, position + cameraFront(), glm::vec3(0.0f, 1.0f, 0.0f));
}

const char* Benchmark::phaseName(Phase p) {
	switch (p) {
	case Phase::Load: return "LOAD";
	case Phase::Spin: return "SPIN";
	case Phase::Flight: return "FLIGHT";
	case Phase::Edits: return "EDITS";
	case Phase::Zoom: return "ZOOM";
	case Phase::Vista: return "VISTA";
	default: return "DONE";
	}
}

std::string Benchmark::status() const {
	std::string text = std::string("BENCHMARK - ") + phaseName(phase) + " " + fixed(phaseTime, 1);
	switch (phase) {
	case Phase::Spin: text += " / " + fixed(spinSeconds, 0) + " S"; break;
	case Phase::Flight: text += " / " + fixed(flightSeconds, 0) + " S"; break;
	case Phase::Edits: text += " / " + fixed(editSeconds, 1) + " S"; break;
	case Phase::Zoom: text += " / " + fixed(zoomSeconds, 0) + " S"; break;
	case Phase::Vista: text += " / " + fixed(vistaSeconds, 0) + " S"; break;
	default: text += " S"; break;
	}
	return text + "   (ESC TO CANCEL)";
}

std::string Benchmark::screenshotPath() {
	// First frame of the spin (everything loaded, facing +X) and halfway through the flight
	const char* name = nullptr;
	if (screenshotsTaken == 0 && phase == Phase::Spin) name = "spin";
	else if (screenshotsTaken == 1 && phase == Phase::Flight && phaseTime >= flightSeconds * 0.5) name = "flight";
	else if (screenshotsTaken == 2 && phase == Phase::Zoom && phaseTime >= 5.5) name = "zoom";//8x, 1.5 s after zooming in
	else if (screenshotsTaken == 3 && phase == Phase::Vista && phaseTime >= vistaRiseSeconds) name = "vista";
	if (!name) return "";

	screenshotsTaken++;
	std::error_code error;
	std::filesystem::create_directories(resultsFolder, error);
	return std::string(resultsFolder) + "/" + runName + "_" + name + ".bmp";
}

// ---------- running ----------

void Benchmark::recordFrame(const FrameTimes& times, const RenderStats& render, const UpdateTimes& parts) {
	if (phase == Phase::Done) return;
	PhaseRecord& r = record();
	r.frameMs.push_back(times.frameMs);
	r.updateList.push_back(times.updateMs);
	r.renderList.push_back(times.renderMs);
	r.gpuList.push_back(times.gpuMs);
	r.updateParts.push_back(parts);
	r.updateMs += times.updateMs;
	r.renderMs += times.renderMs;
	r.gpuMs += times.gpuMs;
	r.gpuShadowMs += times.gpuShadowMs;
	r.triangles += render.triangles;
	r.drawCalls += render.drawCommands;//one command per section, comparable with the old one draw call per chunk
}

void Benchmark::update(World& world, double deltaSeconds) {
	if (phase == Phase::Done) return;
	phaseTime += deltaSeconds;

	switch (phase) {
	case Phase::Load:
		if (world.isFullyLoaded()) {
			startPhase(Phase::Spin, world);
		}
		else if (phaseTime > loadTimeout) {
			loadTimedOut = true;
			startPhase(Phase::Spin, world);
		}
		break;
	case Phase::Spin:
		if (phaseTime >= spinSeconds) startPhase(Phase::Flight, world);
		break;
	case Phase::Flight:
		if (phaseTime >= flightSeconds) {
			startPhase(Phase::Edits, world);
			// In front of and below the camera, where it's looking during the edit phase
			editOrigin = glm::ivec3(glm::floor(position)) + glm::ivec3(8, -8, -4);
		}
		break;
	case Phase::Edits:
		doEdits(world);
		if (phaseTime >= editSeconds) {
			finalStats = world.getStats();//before zooming adds detail, so memory matches older runs
			processMemory(processBytes, peakProcessBytes);
			startPhase(Phase::Zoom, world);
		}
		break;
	case Phase::Zoom:
		if (phaseTime >= zoomSeconds) {
			zoomLevel = 1.0f;
			startPhase(Phase::Vista, world);
		}
		break;
	case Phase::Vista:
		if (phaseTime >= vistaSeconds) {
			endPhase(world);
			phase = Phase::Done;
		}
		break;
	default:
		break;
	}
	placeCamera();
}

void Benchmark::startPhase(Phase next, const World& world) {
	if (next != Phase::Load) endPhase(world);
	phase = next;
	phaseTime = 0.0;

	WorldStats stats = world.getStats();
	PhaseRecord& r = record();
	r.generationStart = stats.generation;
	r.meshStart = stats.meshing;
	r.editStart = stats.editLatency;
}

void Benchmark::endPhase(const World& world) {
	WorldStats stats = world.getStats();
	PhaseRecord& r = record();
	r.seconds = phaseTime;
	r.generationEnd = stats.generation;
	r.meshEnd = stats.meshing;
	r.editEnd = stats.editLatency;
	if (phase == Phase::Load) chunksAfterLoad = stats.chunksLoaded;
}

void Benchmark::placeCamera() {
	// Positions come from the time into the phase, not from adding up movement each frame,
	// so the path is identical whatever the frame rate
	switch (phase) {
	case Phase::Load:
	case Phase::Spin:
		position = glm::vec3(0.5f, pathHeight(0.0f), 0.5f);
		yaw = phase == Phase::Spin ? static_cast<float>(360.0 * phaseTime / spinSeconds) : 0.0f;
		pitch = -15.0f;
		break;
	case Phase::Flight: {
		float x = flightSpeed * static_cast<float>(phaseTime);
		position = glm::vec3(x + 0.5f, pathHeight(x), 0.5f);
		yaw = 0.0f;//+X
		pitch = -12.0f;
		break;
	}
	case Phase::Edits:
		pitch = -35.0f;//stays where the flight ended, looking down at the edits
		break;
	case Phase::Zoom: {
		// Same spot, looking out at the horizon and turning. Zoom steps like pressing C then scrolling
		float t = static_cast<float>(phaseTime);
		pitch = -5.0f;
		yaw = zoomPanSpeed * t;
		if (t < 1.0f) zoomLevel = 1.0f;
		else if (t < 4.0f) zoomLevel = 4.0f;
		else if (t < 6.0f) zoomLevel = 8.0f;
		else if (t < 8.0f) zoomLevel = 16.0f;
		else if (t < 9.0f) zoomLevel = 1.0f;
		else zoomLevel = 4.0f;
		break;
	}
	case Phase::Vista: {
		// Straight up from where the flight ended, so no chunks load and it's only drawing that's measured
		float rise = std::min(static_cast<float>(phaseTime / vistaRiseSeconds), 1.0f);
		rise = rise * rise * (3.0f - 2.0f * rise);//eases in and out, this one's nice to watch
		position.y = glm::mix(pathHeight(flightSpeed * static_cast<float>(flightSeconds)), vistaHeight, rise);
		pitch = glm::mix(-5.0f, vistaPitch, rise);
		float panTime = std::max(static_cast<float>(phaseTime - vistaRiseSeconds), 0.0f);
		yaw = zoomPanSpeed * static_cast<float>(zoomSeconds) + 360.0f * panTime / static_cast<float>(vistaPanSeconds);
		break;
	}
	default:
		break;
	}
}

void Benchmark::doEdits(World& world) {
	// Fill an 8x8 square with stone one block at a time, then start removing them again
	int due = std::min(editCount, static_cast<int>(phaseTime / editInterval));
	while (editsDone < due) {
		int cell = editsDone % 64;
		BlockType type = (editsDone / 64) % 2 == 0 ? BlockType::STONE : BlockType::AIR;
		world.setBlock(editOrigin + glm::ivec3(cell % 8, 0, cell / 8), type);
		editsDone++;
	}
}

// ---------- results ----------

std::vector<std::string> Benchmark::finish() {
	const PhaseRecord& load = records[static_cast<int>(Phase::Load)];
	const PhaseRecord& spin = records[static_cast<int>(Phase::Spin)];
	const PhaseRecord& flight = records[static_cast<int>(Phase::Flight)];
	const PhaseRecord& edits = records[static_cast<int>(Phase::Edits)];
	const PhaseRecord& zoom = records[static_cast<int>(Phase::Zoom)];
	const PhaseRecord& vista = records[static_cast<int>(Phase::Vista)];
	FrameSummary spinFrames = spin.frames();
	FrameSummary flightFrames = flight.frames();
	FrameSummary editFrames = edits.frames();

#ifdef _DEBUG
	const char* build = "DEBUG";
#else
	const char* build = "RELEASE";
#endif
	std::string gpu = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

	double loadGenerateMs = load.generationEnd.averageMsSince(load.generationStart);
	double loadMeshMs = load.meshEnd.averageMsSince(load.meshStart);
	int64_t streamed = flight.generationEnd.count - flight.generationStart.count;
	double streamedPerSecond = flight.seconds > 0.0 ? streamed / flight.seconds : 0.0;
	double editLatencyMs = edits.editEnd.averageMsSince(edits.editStart);
	int64_t editsShown = edits.editEnd.count - edits.editStart.count;
	double editMeshMs = edits.meshEnd.averageMsSince(edits.meshStart);
	double spinTriangles = spin.average(static_cast<double>(spin.triangles));
	double spinDrawCalls = spin.average(static_cast<double>(spin.drawCalls));

	auto frameLine = [&](const char* name, const PhaseRecord& r, const FrameSummary& f) {
		return std::string(name) + "AVG " + fixed(f.averageFps, 0) + " FPS  1% LOW " + fixed(f.onePercentLowFps, 0)
			+ "  CPU " + fixed(r.average(r.updateMs + r.renderMs), 2) + " MS  GPU " + fixed(r.average(r.gpuMs), 2) + " MS";
	};

	std::vector<std::string> lines = {
		"SEED " + std::to_string(seed) + "  RENDER DISTANCE " + std::to_string(renderDistance) + "  " + lodText
			+ "  " + std::to_string(screenWidth) + "X" + std::to_string(screenHeight) + "  " + build,
		gpu,
		"",
		"LOAD    " + std::to_string(chunksAfterLoad) + " CHUNKS IN " + fixed(load.seconds, 2) + " S" + (loadTimedOut ? " (TIMED OUT)" : "")
			+ "  GENERATE " + fixed(loadGenerateMs, 2) + " MS  MESH " + fixed(loadMeshMs, 2) + " MS PER CHUNK",
		frameLine("SPIN    ", spin, spinFrames),
		frameLine("FLIGHT  ", flight, flightFrames) + "  " + fixed(streamedPerSecond, 0) + " CHUNKS/S",
		"EDITS   " + std::to_string(editsShown) + " EDITS  EDIT TO SCREEN " + fixed(editLatencyMs, 1) + " MS  MESH " + fixed(editMeshMs, 2) + " MS",
		"SCENE   " + fixed(spinTriangles / 1'000'000.0, 2) + "M TRIANGLES  " + fixed(spinDrawCalls, 0) + " DRAWS",
		"MEMORY  BLOCKS " + megabytes(finalStats.blockBytes) + " MB  MESHES " + megabytes(finalStats.meshBytes) + " MB  WHOLE GAME "
			+ megabytes(processBytes) + " MB (PEAK " + megabytes(peakProcessBytes) + " MB)",
		frameLine("VISTA   ", vista, vista.frames()) + "  " + fixed(vista.average(static_cast<double>(vista.triangles)) / 1'000'000.0, 2)
			+ "M TRIANGLES  " + fixed(vista.average(static_cast<double>(vista.drawCalls)), 0) + " DRAWS",
	};

	// ---- files ----
	std::error_code error;
	std::filesystem::create_directories(resultsFolder, error);
	std::string reportPath = std::string(resultsFolder) + "/" + runName + ".txt";

	std::ofstream report(reportPath);
	if (report) {
		report << "Benchmark " << timestamp("%Y-%m-%d %H:%M:%S") << "\n";
		for (const std::string& line : lines) report << line << "\n";

		// Full detail per phase, the screen only has room for the headline numbers
		report << "\nphase    seconds  frames  avg_fps  1%_low_fps  worst_ms  update_ms  render_ms  gpu_ms  shadow_ms  triangles  draw_cmds\n";
		const PhaseRecord* phases[] = { &load, &spin, &flight, &edits, &zoom, &vista };
		const char* names[] = { "load", "spin", "flight", "edits", "zoom", "vista" };
		const int phaseCount = 6;
		for (int i = 0; i < phaseCount; i++) {
			const PhaseRecord& r = *phases[i];
			FrameSummary f = r.frames();
			char row[256];
			std::snprintf(row, sizeof(row), "%-8s %7.2f  %6d  %7.1f  %10.1f  %8.2f  %9.3f  %9.3f  %6.3f  %9.3f  %9.0f  %10.0f\n",
				names[i], r.seconds, f.frames, f.averageFps, f.onePercentLowFps, f.worstMs,
				r.average(r.updateMs), r.average(r.renderMs), r.average(r.gpuMs), r.average(r.gpuShadowMs),
				r.average(static_cast<double>(r.triangles)), r.average(static_cast<double>(r.drawCalls)));
			report << row;
		}

		// Each part on its own, so a stutter can be traced to the CPU update, CPU render or the GPU
		// (they don't add up to the frame: the CPU and GPU work at the same time)
		report << "\nslowest 1% of frames (99th percentile, ms)\nphase       frame   update   render      gpu\n";
		for (int i = 0; i < phaseCount; i++) {
			const PhaseRecord& r = *phases[i];
			char row[128];
			std::snprintf(row, sizeof(row), "%-8s %8.2f %8.2f %8.2f %8.2f\n", names[i],
				percentile99(r.frameMs), percentile99(r.updateList), percentile99(r.renderList), percentile99(r.gpuList));
			report << row;
		}

		// Inside World::update, same percentile for each part on its own
		report << "\nslowest 1% of world update parts (99th percentile, ms)\nphase        area    apply     load   upload   launch    merge      lod      far\n";
		for (int i = 0; i < phaseCount; i++) {
			const PhaseRecord& r = *phases[i];
			auto part = [&r](float UpdateTimes::* member) {
				std::vector<float> values;
				values.reserve(r.updateParts.size());
				for (const UpdateTimes& t : r.updateParts) values.push_back(t.*member);
				return percentile99(values);
			};
			char row[128];
			std::snprintf(row, sizeof(row), "%-8s %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f\n", names[i], part(&UpdateTimes::areaMs),
				part(&UpdateTimes::applyMs), part(&UpdateTimes::loadMs), part(&UpdateTimes::uploadMs), part(&UpdateTimes::launchMs),
				part(&UpdateTimes::mergeMs), part(&UpdateTimes::lodMs), part(&UpdateTimes::farMs));
			report << row;
		}

		// Average of each part, shows what a phase spends most of its time on (where the worst tables show spikes)
		report << "\naverage world update parts (ms)\nphase        area    apply     load   upload   launch    merge      lod      far\n";
		for (int i = 0; i < phaseCount; i++) {
			const PhaseRecord& r = *phases[i];
			auto average = [&r](float UpdateTimes::* member) {
				double total = 0.0;
				for (const UpdateTimes& t : r.updateParts) total += t.*member;
				return r.updateParts.empty() ? 0.0 : total / r.updateParts.size();
			};
			char row[128];
			std::snprintf(row, sizeof(row), "%-8s %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f\n", names[i], average(&UpdateTimes::areaMs),
				average(&UpdateTimes::applyMs), average(&UpdateTimes::loadMs), average(&UpdateTimes::uploadMs), average(&UpdateTimes::launchMs),
				average(&UpdateTimes::mergeMs), average(&UpdateTimes::lodMs), average(&UpdateTimes::farMs));
			report << row;
		}

		// The single worst frame of each part: things that happen once in a while (crossing into another chunk,
		// a zoom) are under 1% of frames, so the table above can't show them
		report << "\nworst single frame of each world update part (ms)\nphase        area    apply     load   upload   launch    merge      lod      far\n";
		for (int i = 0; i < phaseCount; i++) {
			const PhaseRecord& r = *phases[i];
			auto worst = [&r](float UpdateTimes::* member) {
				float highest = 0.0f;
				for (const UpdateTimes& t : r.updateParts) highest = std::max(highest, t.*member);
				return highest;
			};
			char row[128];
			std::snprintf(row, sizeof(row), "%-8s %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f\n", names[i], worst(&UpdateTimes::areaMs),
				worst(&UpdateTimes::applyMs), worst(&UpdateTimes::loadMs), worst(&UpdateTimes::uploadMs), worst(&UpdateTimes::launchMs),
				worst(&UpdateTimes::mergeMs), worst(&UpdateTimes::lodMs), worst(&UpdateTimes::farMs));
			report << row;
		}
	}

	// One row per run, so runs from different versions line up in a spreadsheet
	std::string historyPath = std::string(resultsFolder) + "/history.csv";
	bool newFile = !std::filesystem::exists(historyPath, error);
	std::ofstream history(historyPath, std::ios::app);
	if (history) {
		if (newFile) {
			history << "date,build,gpu,width,height,render_distance,load_s,load_chunks,generate_ms,mesh_ms,"
				"spin_fps,spin_1pct_low,spin_cpu_ms,spin_gpu_ms,flight_fps,flight_1pct_low,flight_worst_ms,streamed_chunks_per_s,"
				"edit_to_screen_ms,edit_fps,triangles,draw_calls,block_mb,mesh_mb\n";
		}
		history << timestamp("%Y-%m-%d %H:%M:%S") << "," << build << ",\"" << gpu << "\","
			<< screenWidth << "," << screenHeight << "," << renderDistance << ","
			<< fixed(load.seconds, 2) << "," << chunksAfterLoad << "," << fixed(loadGenerateMs, 3) << "," << fixed(loadMeshMs, 3) << ","
			<< fixed(spinFrames.averageFps, 1) << "," << fixed(spinFrames.onePercentLowFps, 1) << ","
			<< fixed(spin.average(spin.updateMs + spin.renderMs), 3) << "," << fixed(spin.average(spin.gpuMs), 3) << ","
			<< fixed(flightFrames.averageFps, 1) << "," << fixed(flightFrames.onePercentLowFps, 1) << "," << fixed(flightFrames.worstMs, 2) << ","
			<< fixed(streamedPerSecond, 1) << "," << fixed(editLatencyMs, 2) << "," << fixed(editFrames.averageFps, 1) << ","
			<< fixed(spinTriangles, 0) << "," << fixed(spinDrawCalls, 0) << ","
			<< megabytes(finalStats.blockBytes) << "," << megabytes(finalStats.meshBytes) << "\n";
	}

	bool saved = report && history;
	if (!saved) std::cerr << "Couldn't write benchmark results to " << resultsFolder << std::endl;
	lines.push_back("");
	lines.push_back(saved ? "SAVED TO " + reportPath + " AND HISTORY.CSV" : "COULDN'T SAVE THE RESULTS FILE");
	for (const std::string& line : lines) std::cout << line << "\n";
	return lines;
}
