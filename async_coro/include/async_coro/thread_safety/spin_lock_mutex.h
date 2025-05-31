#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <atomic>
#include <thread>

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
    while (_lock.exchange(true, std::memory_order_acquire)) {
      // Wait for lock to be released without generating cache misses
      while (_lock.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
      }
    }
  }

  bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {
    // First do a relaxed load to check if lock is free in order to prevent
    // unnecessary cache misses if someone does while(!try_lock())
    return !_lock.load(std::memory_order_relaxed) && !_lock.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept CORO_THREAD_RELEASE() {
    _lock.store(false, std::memory_order_release);
  }

 private:
  std::atomic<bool> _lock = {false};
};

}  // namespace async_coro
