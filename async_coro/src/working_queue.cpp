#include <async_coro/config.h>
#include <async_coro/working_queue.h>

namespace async_coro
{
	working_queue::~working_queue()
	{
		_num_threads_to_destroy.fetch_add((int)(_num_alive_threads), std::memory_order_release);
		_condition.notify_all();

		{
			std::unique_lock lock {_threads_mutex};
			for (auto& thread : _threads) {
				if (thread.joinable()) {
					thread.join();
				}
			}
			_threads.clear();
			_has_workers.store(false, std::memory_order_release);
		}

		{
			std::unique_lock lock {_mutex};

			// execute all rest tasks
			while (!_tasks.empty()) {
				auto f = std::move(_tasks.front());
				_tasks.pop();

				lock.unlock();

				f();
				f = nullptr;

				lock.lock();
			}
		}
	}

	void working_queue::execute(move_only_function<void()> f)
	{
		ASYNC_CORO_ASSERT(_has_workers.load(std::memory_order_acquire));

		std::unique_lock lock {_mutex};

		_tasks.push(std::move(f));
		
		if (_num_sleeping_threads != 0) {
			lock.unlock();
			_condition.notify_one();
		}
	}

	void working_queue::set_num_threads(std::uint32_t num)
	{
		std::unique_lock lock {_threads_mutex};

		_num_threads = num;

		if (_num_alive_threads > _num_threads) {
			_num_threads_to_destroy.fetch_add((int)(_num_alive_threads - _num_threads), std::memory_order_release);
			_num_alive_threads = _num_threads;
			_has_workers.store(_num_threads > 0, std::memory_order_release);
			_condition.notify_all();
		}
		else {
			start_up_threads();
		}
	}

	bool working_queue::is_current_thread_worker() noexcept
	{
		if (!_has_workers.load(std::memory_order_acquire)) {
			// no workers at all
			return false;
		}

		const auto id = std::this_thread::get_id();

		std::unique_lock lock {_threads_mutex};

		for (const auto& thread : _threads) {
			if (thread.get_id() == id) {
				return true;
			}
		}

		return false;
	}

	void working_queue::start_up_threads()
	{
		if (_num_alive_threads == _num_threads) {
			return;
		}

		_has_workers.store(_num_threads > 0, std::memory_order_release);

		while (_num_threads > _num_alive_threads) {
			_threads.emplace_back([this]() {
				std::unique_lock lock {_mutex};

				while (true) {
					int to_destroy = _num_threads_to_destroy.load(std::memory_order_acquire);

					// if there is no work to do - go to sleep
					if (to_destroy == 0 && _tasks.empty()) {
						_num_sleeping_threads++;

						_condition.wait(lock, [this]() {
							return _num_threads_to_destroy.load(std::memory_order_acquire) > 0 || !_tasks.empty();
						});

						_num_sleeping_threads--;

						to_destroy = _num_threads_to_destroy.load(std::memory_order_acquire);
					}

					// maybe it's time for retirement?
					while (to_destroy > 0) {
						if (_num_threads_to_destroy.compare_exchange_weak(to_destroy, to_destroy - 1, std::memory_order_release)) {
							// our work is done
							return;
						}
						to_destroy = _num_threads_to_destroy.load(std::memory_order_acquire);
					}

					// do some work
					if (!_tasks.empty()) {
						auto f = std::move(_tasks.front());
						_tasks.pop();

						lock.unlock();

						f();
						f = nullptr;

						lock.lock();
					}
				}
			});
		}
	}
}
