#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <async_coro/move_only_function.h>
#include <cstddef>

namespace async_coro
{
	class working_queue
	{
	public:
		working_queue() = default;
		~working_queue();

		// plan function for execution
		void execute(move_only_function<void()> f);

		// seting up num of worker threads and create all of them or stops
		void set_num_threads(std::uint32_t num);
	
		bool is_current_thread_worker() noexcept;

	private:
		void start_up_threads();

	private:
		std::mutex _mutex;
		std::mutex _threads_mutex;
		std::condition_variable _condition; // guarded by _mutex
		std::queue<move_only_function<void()>> _tasks; // guarded by _mutex
		std::vector<std::thread> _threads; // guarded by _threads_mutex
		std::uint32_t _num_threads = 0; // guarded by _threads_mutex
		std::uint32_t _num_alive_threads = 0; // guarded by _threads_mutex
		std::uint32_t _num_sleeping_threads = 0; // guarded by _mutex
		std::atomic_int _num_threads_to_destroy = 0;
		std::atomic_bool _has_workers = false;
	};
}
