#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <atomic>

namespace async_coro {

// Spin lock mutex. Or unfair mutex without excessive sys calls
class CORO_THREAD_CAPABILITY("mutex") spin_lock_mutex {
 public:
  using super = spin_lock_mutex;

  spin_lock_mutex() noexcept = default;
  spin_lock_mutex(const spin_lock_mutex&) = delete;
  spin_lock_mutex(spin_lock_mutex&&) = delete;

  ~spin_lock_mutex() noexcept = default;

  spin_lock_mutex& operator=(const spin_lock_mutex&) = delete;
  spin_lock_mutex& operator=(spin_lock_mutex&&) = delete;

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
