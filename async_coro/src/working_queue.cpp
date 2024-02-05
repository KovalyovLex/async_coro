#include <async_coro/config.h>
#include <async_coro/working_queue.h>

namespace async_coro {
working_queue::~working_queue() {
  _num_threads_to_destroy.fetch_add(_num_alive_threads.load(std::memory_order::acquire),
                                    std::memory_order_release);

  while (_num_sleeping_threads.load(std::memory_order_relaxed) > 0) {
    _num_sleeping_threads.store(0, std::memory_order_relaxed);
    _num_sleeping_threads.notify_all();
    if (_num_threads_to_destroy.load(std::memory_order_relaxed) == 0) {
      break;
    }
    std::this_thread::yield();
  }

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
    // execute all rest tasks
    std::pair<task_function, task_id> task_pair;
    while (_tasks.try_pop(task_pair)) {
      task_pair.first();
    }
  }
}

void working_queue::execute(task_function f) {
  ASYNC_CORO_ASSERT(_num_alive_threads.load(std::memory_order::relaxed) > 0);

  _tasks.push(std::move(f), _current_id.fetch_add(1, std::memory_order::relaxed));

  if (_num_sleeping_threads.load(std::memory_order::relaxed) != 0) {
    _num_sleeping_threads.fetch_sub(1, std::memory_order::relaxed);
    _num_sleeping_threads.notify_one();
  }
}

void working_queue::set_num_threads(uint32_t num) {
  if (_num_alive_threads.load(std::memory_order::relaxed) == num) {
    return;
  }

  std::unique_lock lock{_threads_mutex};

  const auto num_alive_threads = _num_alive_threads.load(std::memory_order::acquire);

  if (num_alive_threads == num) {
    return;
  }

  _num_threads = num;

  if (num_alive_threads > _num_threads) {
    _num_threads_to_destroy.fetch_add((int)(num_alive_threads - _num_threads),
                                      std::memory_order::release);
    _num_alive_threads.store(_num_threads, std::memory_order::release);
    if (_num_sleeping_threads.load(std::memory_order::relaxed) != 0) {
      _num_sleeping_threads.store(0, std::memory_order::relaxed);
      _num_sleeping_threads.notify_all();
    }
  } else {
    start_up_threads();
  }
}

bool working_queue::is_current_thread_worker() const noexcept {
  if (_num_alive_threads.load(std::memory_order::relaxed) == 0) {
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

void working_queue::start_up_threads()  // guarded by _threads_mutex
{
  // cleanup finished threads first
  const auto it =
      std::remove_if(_threads.begin(), _threads.end(),
                     [](auto&& thread) { return !thread.joinable(); });
  if (it != _threads.end()) {
    _threads.erase(it);
  }

  auto num_alive_threads = _num_alive_threads.load(std::memory_order::acquire);

  while (_num_threads > num_alive_threads) {
    _threads.emplace_back([this]() {
      while (true) {
        auto to_destroy = _num_threads_to_destroy.load(std::memory_order::acquire);

        // if there is no work to do - go to sleep
        std::pair<task_function, task_id> task_pair;

        if (to_destroy == 0) {
          // try to get some work more than once as there is not zero chance to do fail pop on non empty q
          if (!_tasks.try_pop(task_pair)) {
            std::this_thread::yield();
            if (!_tasks.try_pop(task_pair)) {
              std::this_thread::yield();
              if (!_tasks.try_pop(task_pair)) {
                const auto num_workers = _num_sleeping_threads.fetch_add(1, std::memory_order::relaxed) + 1;

                _num_sleeping_threads.wait(num_workers, std::memory_order::relaxed);

                to_destroy = _num_threads_to_destroy.load(std::memory_order::acquire);
              }
            }
          }
        }

        // maybe it's time for retirement?
        while (to_destroy > 0) {
          if (_num_threads_to_destroy.compare_exchange_weak(to_destroy, to_destroy - 1, std::memory_order::release, std::memory_order::relaxed)) {
            // our work is done
            return;
          }
        }

        // do some work
        if (task_pair.first) {
          task_pair.first();
        }

        while (_tasks.try_pop(task_pair)) {
          task_pair.first();

          if (_num_threads_to_destroy.load(std::memory_order::relaxed) != 0) [[unlikely]] {
            break;
          }
        }
      }
    });
    num_alive_threads++;
  }

  _num_alive_threads.store(num_alive_threads, std::memory_order_release);
}
}  // namespace async_coro
