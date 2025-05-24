#include <async_coro/config.h>
#include <async_coro/internal/thread_safety/unique_lock.h>
#include <async_coro/working_queue.h>

#include <algorithm>

namespace async_coro {
working_queue::~working_queue() {
  _num_threads_to_destroy.fetch_add(_num_alive_threads.load(std::memory_order::acquire),
                                    std::memory_order::relaxed);

  std::vector<std::thread> threads;

  {
    unique_lock lock{_threads_mutex};

    threads.swap(_threads);
    _num_alive_threads.store(0, std::memory_order::release);
  }

  // notify without lock
  try_to_awake_thread(true);

  // join threads without lock
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads.clear();

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

  try_to_awake_thread(false);
}

void working_queue::set_num_threads(uint32_t num) {
  if (_num_alive_threads.load(std::memory_order::relaxed) == num) {
    return;
  }

  unique_lock lock{_threads_mutex};

  const auto num_alive_threads = _num_alive_threads.load(std::memory_order::acquire);

  if (num_alive_threads == num) {
    return;
  }

  _num_threads = num;

  if (num_alive_threads > _num_threads) {
    _num_threads_to_destroy.fetch_add((int)(num_alive_threads - _num_threads),
                                      std::memory_order::relaxed);
    _num_alive_threads.store(_num_threads, std::memory_order::release);

    try_to_awake_thread(true);
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

  unique_lock lock{_threads_mutex};

  for (const auto& thread : _threads) {
    if (thread.get_id() == id) {
      return true;
    }
  }

  return false;
}

void working_queue::try_to_awake_thread(bool multiple) noexcept {
  if (_num_sleeping_threads.load(std::memory_order::acquire) != 0) {
    _await_changes.fetch_add(1, std::memory_order::relaxed);
    if (multiple) {
      _await_changes.notify_all();
    } else {
      _await_changes.notify_one();
    }
  }
}

void working_queue::start_up_threads() COTHREAD_REQUIRES(_threads_mutex) {
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
      auto to_destroy = _num_threads_to_destroy.load(std::memory_order::relaxed);
      int num_failed_tries = 0;
      constexpr int max_num_fails_before_sleep = 25;
      while (true) {
        std::pair<task_function, task_id> task_pair;

        if (to_destroy == 0) [[likely]] {
          // try to pick some work
          if (!_tasks.try_pop(task_pair)) {
            num_failed_tries++;

            // sleep only if there is no work for some period
            if (num_failed_tries >= max_num_fails_before_sleep) {
              to_destroy = _num_threads_to_destroy.load(std::memory_order::relaxed);
              if (to_destroy == 0) [[likely]] {
                num_failed_tries = 0;  // reset counter tries

                const auto current = _await_changes.load(std::memory_order::acquire);

                // we need to change number after store await changes
                _num_sleeping_threads.fetch_add(1, std::memory_order::release);

                _await_changes.wait(current, std::memory_order::relaxed);

                _num_sleeping_threads.fetch_sub(1, std::memory_order::relaxed);
              }
            }
          } else {
            // do some work
            if (task_pair.first) {
              task_pair.first();
              task_pair.first = nullptr;
              num_failed_tries = 0;
            }
          }
        }

        // maybe it's time for retirement?
        while (to_destroy > 0) [[unlikely]] {
          if (_num_threads_to_destroy.compare_exchange_weak(to_destroy, to_destroy - 1, std::memory_order::relaxed)) {
            // our work is done
            return;
          }
        }

        while (to_destroy == 0 && _tasks.try_pop(task_pair)) {
          task_pair.first();
          num_failed_tries = 0;
          to_destroy = _num_threads_to_destroy.load(std::memory_order::relaxed);
        }
        num_failed_tries++;
        if (to_destroy == 0) [[likely]] {
          // yield before next try
          std::this_thread::yield();
        }
      }
    });
    num_alive_threads++;
  }

  _num_alive_threads.store(num_alive_threads, std::memory_order::release);
}
}  // namespace async_coro
