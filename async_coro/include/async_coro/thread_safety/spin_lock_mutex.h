#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <atomic>

namespace async_coro {

// Spin lock mutex. Or unfair mutex without excessive sys calls
class CORO_THREAD_CAPABILITY("mutex") spin_lock_mutex {
 public:
  using super = spin_lock_mutex;

  spin_lock_mutex() = default;
  spin_lock_mutex(const spin_lock_mutex&) = delete;
  spin_lock_mutex& operator=(const spin_lock_mutex&) = delete;

  void lock() noexcept CORO_THREAD_ACQUIRE() {
    // Optimistically assume the lock is free on first the try
    while (_lock.test_and_set(std::memory_order::acquire)) {
      // Wait for lock to be released without generating cache misses
      _lock.wait(true, std::memory_order::relaxed);
    }
  }

  bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {
    return !_lock.test_and_set(std::memory_order::acquire);
  }

  void unlock() noexcept CORO_THREAD_RELEASE() {
    _lock.clear(std::memory_order::release);
    _lock.notify_one();
  }

 private:
  std::atomic_flag _lock{};
};

}  // namespace async_coro
