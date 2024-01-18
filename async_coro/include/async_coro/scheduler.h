#pragma once

#include <async_coro/promise.h>
#include <async_coro/base_handle.h>
#include <async_coro/move_only_function.h>
#include <async_coro/working_queue.h>
#include <async_coro/internal/passkey.h>
#include <vector>
#include <coroutine>
#include <thread>
#include <mutex>

namespace async_coro
{
	template<typename R>
	struct promise;
	
	enum class execution_thread : std::uint8_t
	{
		undefined,
		main_thread,
		worker_thread
	};

    class scheduler
	{
	public:
		scheduler() noexcept = default;
		~scheduler();

		// Executes planned tasks for main thread
		void update();

		template<typename R>
		void start_task(promise<R> coro, execution_thread thread = execution_thread::main_thread) {
			add_coroutine(coro.release_handle(internal::passkey{ this }), thread);
		}

		working_queue& get_working_queue() noexcept { return _queue; }

		bool is_current_thread_fits(execution_thread thread) const noexcept;

		// TODO: make it private
		void on_child_coro_added(base_handle& parent, std::coroutine_handle<base_handle> child_handle);
		void continue_execution(std::coroutine_handle<base_handle> handle);
		void change_thread(std::coroutine_handle<base_handle> handle, execution_thread thread);

	private:
		void add_coroutine(std::coroutine_handle<base_handle> handle, execution_thread thread);
		void continue_execution_impl(std::coroutine_handle<base_handle> handle, base_handle& handle_impl);
		void check_main_thread_detected();

	private:
		std::mutex _task_mutex;
		std::mutex _mutex;
		working_queue _queue;
		std::vector<std::coroutine_handle<base_handle>> _managed_coroutines; // guarded by _mutex
		std::vector<move_only_function<void(scheduler&)>> _update_tasks;
		std::vector<move_only_function<void(scheduler&)>> _update_tasks_syncronized; // guarded by _task_mutex
		std::thread::id _main_thread = {};
		bool _is_destroying = false; // guarded by _mutex
		std::atomic_bool _has_syncronized_tasks = false;
    };
}
