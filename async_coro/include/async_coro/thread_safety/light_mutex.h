#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <atomic>

namespace async_coro {

// Fast mutex for case if there is no concurrency (no sys calls for lock, only in unlock)
class CORO_THREAD_CAPABILITY("mutex") light_mutex {
 public:
  using super = light_mutex;

  light_mutex() noexcept = default;
  light_mutex(const light_mutex&) = delete;
  light_mutex(light_mutex&&) = delete;

  ~light_mutex() noexcept = default;

  light_mutex& operator=(const light_mutex&) = delete;
  light_mutex& operator=(light_mutex&&) = delete;

  void lock() noexcept CORO_THREAD_ACQUIRE() {
    bool expected = false;
    while (!_lock.compare_exchange_strong(expected, true, std::memory_order::acquire, std::memory_order::relaxed)) {
      _lock.wait(true, std::memory_order::relaxed);
      expected = false;
    }
  }

  bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {
    bool expected = false;
    return _lock.compare_exchange_strong(expected, true, std::memory_order::acquire, std::memory_order::relaxed);
  }

  void unlock() noexcept CORO_THREAD_RELEASE() {
    _lock.store(false, std::memory_order::release);
    _lock.notify_one();
  }

 private:
  std::atomic_bool _lock{false};
};

}  // namespace async_coro
