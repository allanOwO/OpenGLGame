#include "DebugOverlay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <glad/glad.h>

#include "UI.h"
#include "World.h"
#include "Renderer.h"

namespace {
	const glm::vec4 background(0.0f, 0.0f, 0.0f, 0.55f);
	const glm::vec4 white(0.95f, 0.95f, 0.95f, 1.0f);
	const glm::vec4 heading(1.0f, 0.85f, 0.4f, 1.0f);
	const glm::vec4 grey(0.65f, 0.67f, 0.72f, 1.0f);
	const glm::vec4 good(0.35f, 0.85f, 0.4f, 0.9f);
	const glm::vec4 okay(0.95f, 0.8f, 0.25f, 0.9f);
	const glm::vec4 bad(0.95f, 0.3f, 0.25f, 0.9f);

	std::string fixed(double value, int decimals) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
		return buffer;
	}

	// 1234567 -> "1.23M", keeps the lines short
	std::string compact(int64_t value) {
		if (value >= 1'000'000) return fixed(value / 1'000'000.0, 2) + "M";
		if (value >= 1'000) return fixed(value / 1'000.0, 1) + "K";
		return std::to_string(value);
	}

	std::string megabytes(size_t bytes) {
		return fixed(bytes / (1024.0 * 1024.0), 1) + " MB";
	}

	// Minecraft's names: north is -Z
	std::string facing(const glm::vec3& front) {
		if (std::abs(front.x) > std::abs(front.z)) return front.x > 0.0f ? "EAST (+X)" : "WEST (-X)";
		return front.z > 0.0f ? "SOUTH (+Z)" : "NORTH (-Z)";
	}
}

FrameSummary FrameSummary::of(std::vector<float> frameMs) {
	FrameSummary summary;
	summary.frames = static_cast<int>(frameMs.size());
	if (frameMs.empty()) return summary;

	double total = 0.0;
	for (float ms : frameMs) total += ms;
	summary.averageMs = total / frameMs.size();
	summary.averageFps = total > 0.0 ? 1000.0 * frameMs.size() / total : 0.0;

	// nth_element puts the 99th percentile value in place without sorting everything
	size_t index = std::min(frameMs.size() - 1, static_cast<size_t>(frameMs.size() * 0.99));
	std::nth_element(frameMs.begin(), frameMs.begin() + index, frameMs.end());
	summary.onePercentLowFps = frameMs[index] > 0.0f ? 1000.0 / frameMs[index] : 0.0;
	summary.worstMs = *std::max_element(frameMs.begin() + index, frameMs.end());
	return summary;
}

void DebugOverlay::recordFrame(const FrameTimes& times) {
	history[historyNext] = times.frameMs;
	historyNext = (historyNext + 1) % historySize;

	// The window is only emptied by draw(), so don't let it grow while the overlay is hidden
	if (!visible) return;
	windowFrames.push_back(times.frameMs);
	windowTotals.updateMs += times.updateMs;
	windowTotals.renderMs += times.renderMs;
	windowTotals.gpuMs += times.gpuMs;
	windowTotals.gpuShadowMs += times.gpuShadowMs;
}

void DebugOverlay::updateAverages(const World& world) {
	double now = nowSeconds();
	if (now - windowStart < averageWindow || windowFrames.empty()) return;

	float frames = static_cast<float>(windowFrames.size());
	shownFrames = FrameSummary::of(windowFrames);
	shownTimes.updateMs = windowTotals.updateMs / frames;
	shownTimes.renderMs = windowTotals.renderMs / frames;
	shownTimes.gpuMs = windowTotals.gpuMs / frames;
	shownTimes.gpuShadowMs = windowTotals.gpuShadowMs / frames;

	// Jobs that finished during the window, the old value stays if none did
	WorldStats stats = world.getStats();
	if (stats.generation.count > lastGeneration.count) shownGenerationMs = stats.generation.averageMsSince(lastGeneration);
	if (stats.meshing.count > lastMeshing.count) shownMeshMs = stats.meshing.averageMsSince(lastMeshing);
	if (stats.editLatency.count > lastEditLatency.count) shownEditLatencyMs = stats.editLatency.averageMsSince(lastEditLatency);
	lastGeneration = stats.generation;
	lastMeshing = stats.meshing;
	lastEditLatency = stats.editLatency;

	windowStart = now;
	windowFrames.clear();
	windowTotals = FrameTimes();
}

void DebugOverlay::draw(UI& ui, const World& world, const Renderer& renderer,
	const glm::vec3& cameraPos, const glm::vec3& cameraFront, float yaw, float pitch) {
	if (!visible) return;
	updateAverages(world);
	if (gpuName.empty()) gpuName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

	// A bit smaller than the menus so it covers less of the game
	int textScale = std::max(1, ui.scale() - 1);
	float lineHeight = UIRenderer::textHeight(textScale) + 3.0f * textScale;
	float padding = 2.0f * textScale;

	WorldStats worldStats = world.getStats();
	const RenderStats& render = renderer.getStats();
	glm::ivec3 block = glm::ivec3(glm::floor(cameraPos));
	int chunkX = static_cast<int>(std::floor(cameraPos.x / Chunk::chunkSize));
	int chunkZ = static_cast<int>(std::floor(cameraPos.z / Chunk::chunkSize));

	struct Line { std::string text; glm::vec4 colour; };
	std::vector<Line> lines = {
		{ "DEBUG (F3)", heading },
		{ fixed(shownFrames.averageFps, 0) + " FPS  " + fixed(shownFrames.averageMs, 2) + " MS  1% LOW " + fixed(shownFrames.onePercentLowFps, 0) + " FPS", white },
		{ "CPU UPDATE " + fixed(shownTimes.updateMs, 2) + " MS  RENDER " + fixed(shownTimes.renderMs, 2) + " MS", white },
		{ "GPU " + fixed(shownTimes.gpuMs, 2) + " MS  (SHADOW PASS " + fixed(shownTimes.gpuShadowMs, 2) + " MS)", white },
		{ "", white },
		{ "XYZ " + fixed(cameraPos.x, 1) + " / " + fixed(cameraPos.y, 1) + " / " + fixed(cameraPos.z, 1), white },
		{ "BLOCK " + std::to_string(block.x) + " " + std::to_string(block.y) + " " + std::to_string(block.z)
			+ "  CHUNK " + std::to_string(chunkX) + " " + std::to_string(chunkZ), white },
		{ "FACING " + facing(cameraFront) + "  YAW " + fixed(yaw, 0) + " PITCH " + fixed(pitch, 0), white },
		{ "SEED " + std::to_string(world.getSeed()) + "  RENDER DISTANCE " + std::to_string(world.getFarDistance())
			+ "  DETAIL " + std::to_string(world.getRenderDistance()), white },
		{ "", white },
		{ "RENDERING", heading },
		{ "SECTIONS DRAWN " + std::to_string(render.sectionsDrawn) + "  CULLED " + std::to_string(render.sectionsCulled)
			+ "  REGIONS CULLED " + std::to_string(render.regionsCulled), white },
		{ "DRAW CALLS " + std::to_string(render.drawCalls) + "  SHADOW " + std::to_string(render.shadowDrawCalls), white },
		{ "TRIANGLES " + compact(render.triangles) + "  SHADOW " + compact(render.shadowTriangles), white },
		{ "LOD SECTIONS FULL " + std::to_string(render.sectionsByLod[0]) + "  2X " + std::to_string(render.sectionsByLod[1])
			+ "  4X " + std::to_string(render.sectionsByLod[2]) + "  8X " + std::to_string(render.sectionsByLod[3]), white },
		{ "FAR TERRAIN UNITS " + std::to_string(render.farUnitsDrawn) + " DRAWN OF " + std::to_string(world.getFarUnits().size()), white },
		{ "MERGED REGIONS " + std::to_string(render.mergedRegionsDrawn) + "  WAITING TO MERGE " + std::to_string(worldStats.dirtyRegions)
			+ "  SIMULATION DISTANCE " + std::to_string(world.getSimulationDistance()), white },
		{ "", white },
		{ "WORLD", heading },
		{ "CHUNKS LOADED " + std::to_string(worldStats.chunksLoaded) + "  ACTIVE " + std::to_string(worldStats.chunksActive), white },
		{ "JOBS GENERATING " + std::to_string(worldStats.generationJobs) + "  MESHING " + std::to_string(worldStats.meshJobs)
			+ "  CPU THREADS " + std::to_string(std::thread::hardware_concurrency()), white },
		{ "PER CHUNK GENERATE " + fixed(shownGenerationMs, 2) + " MS  MESH " + fixed(shownMeshMs, 2) + " MS", white },
		{ "BLOCK EDIT TO SCREEN " + fixed(shownEditLatencyMs, 1) + " MS", white },
		{ "MEMORY BLOCKS " + megabytes(worldStats.blockBytes) + "  MESHES " + megabytes(worldStats.meshBytes), white },
		{ "", white },
		{ gpuName, grey },
	};

	// Background sized to each line like Minecraft's, so the world stays visible between them
	float y = padding;
	for (const Line& line : lines) {
		if (!line.text.empty()) {
			ui.panel(0.0f, y - padding * 0.5f, UIRenderer::textWidth(line.text, textScale) + padding * 2.0f, lineHeight, background);
			ui.label(line.text, padding, y + textScale, textScale, line.colour);
		}
		y += lineHeight;
	}

	drawGraph(ui, static_cast<float>(ui.screenWidth()) - padding, static_cast<float>(ui.screenHeight()) - padding, textScale);
}

void DebugOverlay::drawGraph(UI& ui, float right, float bottom, int textScale) {
	// One bar per frame, oldest on the left. The top of the graph is 33 ms (30 fps) and the line is 16.7 ms (60 fps)
	const float maxMs = 33.3f;
	float barWidth = static_cast<float>(textScale);
	float width = barWidth * historySize;
	float height = 60.0f * textScale;
	float left = right - width;
	float top = bottom - height;

	ui.panel(left, top, width, height, background);
	for (int i = 0; i < historySize; i++) {
		float ms = history[(historyNext + i) % historySize];
		if (ms <= 0.0f) continue;//not filled yet
		float barHeight = std::min(ms / maxMs, 1.0f) * height;
		const glm::vec4& colour = ms <= 16.7f ? good : ms <= 33.3f ? okay : bad;
		ui.panel(left + i * barWidth, bottom - barHeight, barWidth, barHeight, colour);
	}

	float sixtyFpsY = bottom - (16.7f / maxMs) * height;
	ui.panel(left, sixtyFpsY, width, static_cast<float>(textScale), glm::vec4(1.0f, 1.0f, 1.0f, 0.5f));
	ui.label("60 FPS", left + textScale * 2.0f, sixtyFpsY - UIRenderer::textHeight(textScale) - textScale, textScale, grey);
	ui.label("FRAME TIME", left + textScale * 2.0f, top + textScale * 2.0f, textScale, grey);
}
