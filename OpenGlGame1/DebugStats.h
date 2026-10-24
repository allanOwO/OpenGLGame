#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

// Numbers the debug overlay (F3) and the benchmark read from the engine
// Kept as plain counters so measuring costs almost nothing when nobody is looking at them

// Seconds since some fixed point, steady_clock never jumps backwards (unlike the wall clock)
inline double nowSeconds() {
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Running total of how long a kind of job took, safe to add to from worker threads
// Readers take two snapshots and subtract them to get the average over that window
struct JobTimer {
	std::atomic<int64_t> totalMicros{ 0 };
	std::atomic<int64_t> count{ 0 };

	void add(double seconds) {
		totalMicros += static_cast<int64_t>(seconds * 1'000'000.0);
		count++;
	}
};

// Plain copy of a JobTimer at one moment
struct JobTotals {
	int64_t totalMicros = 0;
	int64_t count = 0;

	static JobTotals of(const JobTimer& timer) { return { timer.totalMicros.load(), timer.count.load() }; }

	// Average job length in milliseconds between an earlier snapshot and this one, 0 if no jobs finished
	double averageMsSince(const JobTotals& earlier) const {
		int64_t jobs = count - earlier.count;
		return jobs > 0 ? (totalMicros - earlier.totalMicros) / 1000.0 / jobs : 0.0;
	}
};

// What the renderer drew last frame
struct RenderStats {
	int chunksDrawn = 0;
	int chunksCulled = 0;//outside the camera frustum
	int drawCalls = 0;//main pass only
	int64_t triangles = 0;
	int shadowDrawCalls = 0;
	int64_t shadowTriangles = 0;
};

// State of the world's chunks and job queues right now
struct WorldStats {
	int chunksLoaded = 0;
	int chunksActive = 0;//inside render distance
	int generationJobs = 0;//running right now
	int meshJobs = 0;
	JobTotals generation;//time to generate (or restore) one chunk on a worker
	JobTotals meshing;//time to build one chunk mesh on a worker
	JobTotals editLatency;//block edit -> new mesh uploaded, includes waiting in the queue
	size_t blockBytes = 0;//CPU memory for block arrays
	size_t meshBytes = 0;//GPU memory for vertex + index buffers
};
