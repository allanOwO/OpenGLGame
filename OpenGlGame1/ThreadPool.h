#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// A fixed set of worker threads that run jobs from a shared queue
//
// std::async(std::launch::async) is allowed to start a brand new thread for every job, and creating a thread
// costs far more than generating a chunk (~0.15 ms). Here the threads are made once and reused for every job
//
// Jobs run in the order they're submitted, World submits nearest chunks first
// Futures from submit() do NOT wait in their destructor (unlike std::async's), so whoever owns them
// must wait for jobs that use its data before that data is destroyed (see World::~World)
class ThreadPool
{
public:
	explicit ThreadPool(size_t threadCount) {
		for (size_t i = 0; i < threadCount; i++) {
			workers.emplace_back([this] { workerLoop(); });
		}
	}

	~ThreadPool() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		wakeUp.notify_all();
		for (std::thread& worker : workers) worker.join();//jobs already queued still run first
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	size_t size() const { return workers.size(); }

	// Runs job on a worker, the future gets its return value
	template <class Job>
	auto submit(Job job) -> std::future<decltype(job())> {
		using Result = decltype(job());
		// packaged_task can only be moved, std::function needs something copyable, so share it
		auto task = std::make_shared<std::packaged_task<Result()>>(std::move(job));
		std::future<Result> future = task->get_future();
		{
			std::lock_guard<std::mutex> lock(mutex);
			jobs.emplace_back([task] { (*task)(); });
		}
		wakeUp.notify_one();
		return future;
	}

private:
	void workerLoop() {
		for (;;) {
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(mutex);
				// Sleep until there's work, instead of spinning and burning a core
				wakeUp.wait(lock, [this] { return stopping || !jobs.empty(); });
				if (jobs.empty()) return;//stopping and nothing left
				job = std::move(jobs.front());
				jobs.pop_front();
			}
			job();//outside the lock so other workers can take jobs meanwhile
		}
	}

	std::vector<std::thread> workers;
	std::deque<std::function<void()>> jobs;
	std::mutex mutex;
	std::condition_variable wakeUp;
	bool stopping = false;
};
