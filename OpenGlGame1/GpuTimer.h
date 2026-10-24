#pragma once
#include <glad/glad.h>

// Measures how long the GPU spends on the commands between begin() and end()
// The GPU runs behind the CPU, so asking for a query's result straight away would stall until it catches up
// Instead several queries take turns and each one is read a few frames later when it's ready
// Only one GL_TIME_ELAPSED query can run at a time, so timers can follow each other but not nest
class GpuTimer
{
public:
	GpuTimer() = default;
	~GpuTimer() { if (created) glDeleteQueries(queryCount, queries); }
	GpuTimer(const GpuTimer&) = delete;
	GpuTimer& operator=(const GpuTimer&) = delete;

	void begin() {
		if (!created) {//needs a GL context, so made on first use instead of in the constructor
			glGenQueries(queryCount, queries);
			created = true;
		}
		// The query being reused was started queryCount frames ago, read it if the GPU has finished it
		GLuint query = queries[next];
		if (started[next]) {
			GLint available = 0;
			glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
			if (available) {
				GLuint64 nanoseconds = 0;
				glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
				lastMs = static_cast<float>(nanoseconds / 1'000'000.0);
			}
		}
		glBeginQuery(GL_TIME_ELAPSED, query);
		started[next] = true;
		next = (next + 1) % queryCount;
	}

	void end() { glEndQuery(GL_TIME_ELAPSED); }

	void skip() { lastMs = 0.0f; }//the measured work didn't run this frame
	float ms() const { return lastMs; }//a few frames behind

private:
	static const int queryCount = 4;
	GLuint queries[queryCount] = {};
	bool started[queryCount] = {};
	int next = 0;
	bool created = false;
	float lastMs = 0.0f;
};
