#include <async_coro/config.h>
#include <async_coro/working_queue.h>

namespace async_coro {
working_queue::~working_queue() {
  _num_threads_to_destroy.fetch_add(
      _num_alive_threads.load(std::memory_order::acquire),
      std::memory_order_release);
  _condition.notify_all();

  {
    std::unique_lock lock{_threads_mutex};
    for (auto& thread : _threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    _threads.clear();
    _num_alive_threads.store(0, std::memory_order_release);
  }

  {
    std::unique_lock lock{_mutex};

    // execute all rest tasks
    while (!_tasks.empty()) {
      auto f = std::move(_tasks.front());
      _tasks.pop();

      lock.unlock();

      f.first();
      f.first = nullptr;  // cleanup without lock

      lock.lock();
    }
  }
}

working_queue::task_id working_queue::execute(task_function f) {
  ASYNC_CORO_ASSERT(_num_alive_threads.load(std::memory_order_acquire) > 0);

  std::unique_lock lock{_mutex};

  const auto task_id = _current_id++;
  _tasks.push(std::make_pair(std::move(f), task_id));

  if (_num_sleeping_threads != 0) {
    lock.unlock();
    _condition.notify_one();
  }
  return task_id;
}

void working_queue::set_num_threads(uint32_t num) {
  if (_num_alive_threads.load(std::memory_order::acquire) == num) {
    return;
  }

  std::unique_lock lock{_threads_mutex};

  const auto num_alive_threads =
      _num_alive_threads.load(std::memory_order::acquire);

  if (num_alive_threads == num) {
    return;
  }

  _num_threads = num;

  if (num_alive_threads > _num_threads) {
    _num_threads_to_destroy.fetch_add((int)(num_alive_threads - _num_threads),
                                      std::memory_order_release);
    _num_alive_threads.store(_num_threads, std::memory_order_release);
    _condition.notify_all();
  } else {
    start_up_threads();
  }
}

bool working_queue::is_current_thread_worker() noexcept {
  if (_num_alive_threads.load(std::memory_order_acquire) == 0) {
    // no workers at all
    return false;
  }

  const auto id = std::this_thread::get_id();

  std::unique_lock lock{_threads_mutex};

  for (const auto& thread : _threads) {
    if (thread.get_id() == id) {
      return true;
    }
  }

  return false;
}

bool working_queue::is_finished(task_id id) noexcept {
  std::unique_lock lock{_mutex};

  return _tasks.empty() || _tasks.front().second > id;
}

void working_queue::start_up_threads()  // guarded by _threads_mutex
{
  // cleanup finished threads first
  const auto it =
      std::remove_if(_threads.begin(), _threads.end(),
                     [](auto&& thread) { return !thread.joinable(); });
  if (it != _threads.end()) {
    _threads.erase(it);
  }

  auto num_alive_threads = _num_alive_threads.load(std::memory_order_acquire);

  while (_num_threads > num_alive_threads) {
    _threads.emplace_back([this]() {
      while (true) {
        auto to_destroy =
            _num_threads_to_destroy.load(std::memory_order_acquire);

        // if there is no work to do - go to sleep
        if (to_destroy == 0) {
          std::unique_lock lock{_mutex};

          if (_tasks.empty()) {
            _num_sleeping_threads++;

            _condition.wait(lock, [this]() {
              return _num_threads_to_destroy.load(std::memory_order_acquire) >
                         0 ||
                     !_tasks.empty();
            });

            _num_sleeping_threads--;

            to_destroy =
                _num_threads_to_destroy.load(std::memory_order_acquire);
          }
        }

        // maybe it's time for retirement?
        while (to_destroy > 0) {
          if (_num_threads_to_destroy.compare_exchange_weak(
                  to_destroy, to_destroy - 1, std::memory_order_release)) {
            // our work is done
            return;
          }
          to_destroy = _num_threads_to_destroy.load(std::memory_order_acquire);
        }

        // do some work
        std::unique_lock lock{_mutex};

        if (!_tasks.empty()) {
          auto f = std::move(_tasks.front());
          _tasks.pop();

          lock.unlock();

          f.first();
        }
      }
    });
    num_alive_threads++;
  }

  _num_alive_threads.store(num_alive_threads, std::memory_order_release);
}
}  // namespace async_coro
