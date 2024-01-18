#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <thread>

namespace async_coro
{
	scheduler::~scheduler() {
		// ignore all update tasks ar it continue may lead to infinite loop
		_queue.~working_queue();

		std::unique_lock lock {_mutex};
		auto coros = std::move(_managed_coroutines);
		_is_destroying = true;
		lock.unlock();

		for (auto& coro : coros) {
			if (coro) {
				coro.destroy();
			}
		}
	}

	void scheduler::check_main_thread_detected() {
		if (_main_thread == std::thread::id {}) [[unlikely]] {
			// store this thread as main
			_main_thread = std::this_thread::get_id();
		}
	}

	bool scheduler::is_current_thread_fits(execution_thread thread) const noexcept {
		if (thread == execution_thread::main_thread) {
			return _main_thread == std::this_thread::get_id();
		}
		else if (thread == execution_thread::worker_thread) {
			return _queue.is_current_thread_worker();
		}
		return false;
	}

	void scheduler::update() {
		check_main_thread_detected();
		
		ASYNC_CORO_ASSERT(_main_thread == std::this_thread::get_id());

		if (!_update_tasks.empty()) {
			for (size_t i = 0; i < _update_tasks.size(); i++) {
				_update_tasks[i](*this);
			}
			_update_tasks.clear();
		}

		if (_has_syncronized_tasks.load(std::memory_order_acquire)) {
			{
				std::unique_lock lock {_task_mutex};
				_update_tasks.swap(_update_tasks_syncronized);
				_has_syncronized_tasks.store(false, std::memory_order_release);
			}

			for (size_t i = 0; i < _update_tasks.size(); i++) {
				_update_tasks[i](*this);
			}
			_update_tasks.clear();
		}
	}

	void scheduler::continue_execution_impl(std::coroutine_handle<base_handle> handle, base_handle& handle_impl) {
		ASYNC_CORO_ASSERT(handle_impl.is_current_thread_same());

		handle_impl._state = coroutine_state::running;
		handle.resume();
		handle_impl._state = handle.done() ? coroutine_state::finished : coroutine_state::suspended;
	}

	void scheduler::add_coroutine(std::coroutine_handle<base_handle> handle, execution_thread thread) {
		auto& handle_impl = handle.promise();
		ASYNC_CORO_ASSERT(handle_impl._execution_thread == std::thread::id {});
		ASYNC_CORO_ASSERT(handle_impl._state == coroutine_state::created);

		{
			std::unique_lock lock {_mutex};

			if (_is_destroying) {
				// if we are in destructor no way to run this coroutine
				if (handle) {
					handle.destroy();
				}
				return;
			}

			_managed_coroutines.push_back(handle);
		}

		handle_impl._scheduler = this;

		if (is_current_thread_fits(thread)) {
			// start execution immediatelly if we in right thread

			handle_impl._execution_thread = std::this_thread::get_id();
			continue_execution_impl(handle, handle_impl);
		}
		else {
			change_thread(handle, thread);
		}
	}

	void scheduler::continue_execution(std::coroutine_handle<base_handle> handle) {
		auto& handle_impl = handle.promise();
		ASYNC_CORO_ASSERT(handle_impl._execution_thread != std::thread::id {});
		ASYNC_CORO_ASSERT(handle_impl._state == coroutine_state::suspended);

		continue_execution_impl(handle, handle_impl);
	}

	void scheduler::change_thread(std::coroutine_handle<base_handle> handle, execution_thread thread) {
		auto& handle_impl = handle.promise();
		ASYNC_CORO_ASSERT(handle_impl._scheduler == this);
		ASYNC_CORO_ASSERT(!is_current_thread_fits(thread));

		if (thread == execution_thread::main_thread) {
			std::unique_lock lock {_task_mutex};
			_update_tasks_syncronized.push_back([handle](auto& thiz) {
				auto& handle_base = handle.promise();
				handle_base._execution_thread = std::this_thread::get_id();
				thiz.continue_execution_impl(handle, handle_base);
			});
			_has_syncronized_tasks.store(true, std::memory_order_release);
		}
		else {
			_queue.execute([handle, this]() {
				auto& handle_base = handle.promise();
				handle_base._execution_thread = std::this_thread::get_id();
				this->continue_execution_impl(handle, handle_base);
			});
		}
	}

	void scheduler::on_child_coro_added(base_handle& parent, std::coroutine_handle<base_handle> child_handle) {
		ASYNC_CORO_ASSERT(parent._scheduler == this);

		auto& child = child_handle.promise();
		ASYNC_CORO_ASSERT(child._execution_thread == std::thread::id {});
		ASYNC_CORO_ASSERT(child._state == coroutine_state::created);

		child._scheduler = this;
		child._execution_thread = parent._execution_thread;
		child._parent = &parent;
		child._state = coroutine_state::suspended;
		
		// start execution of internal coroutine
		continue_execution_impl(child_handle, child);
	}
}
