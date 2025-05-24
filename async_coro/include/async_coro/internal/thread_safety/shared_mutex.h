#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

#include <shared_mutex>

namespace async_coro {

template <typename T>
class unique_lock;

template <typename T>
class shared_lock_guard;

// Replacement for std::mutex to work with clang thread safety analysis
class COTHREAD_CAPABILITY("mutex") shared_mutex : protected std::shared_mutex {
  friend unique_lock<shared_mutex>;
  friend shared_lock_guard<shared_mutex>;

 public:
  using super = std::shared_mutex;

  shared_mutex() = default;
  shared_mutex(const shared_mutex&) = delete;
  shared_mutex& operator=(const shared_mutex&) = delete;

  void lock() COTHREAD_ACQUIRE() {
    super::lock();
  }

  [[nodiscard]] bool try_lock() noexcept COTHREAD_TRY_ACQUIRE(true) {
    return super::try_lock();
  }

  void unlock() noexcept COTHREAD_RELEASE() {
    super::unlock();
  }

  void lock_shared() noexcept COTHREAD_ACQUIRE_SHARED() {
    super::lock_shared();
  }

  void unlock_shared() noexcept COTHREAD_RELEASE_SHARED() {
    super::unlock_shared();
  }

  [[nodiscard]] bool try_lock_shared() noexcept COTHREAD_TRY_ACQUIRE_SHARED(true) {
    return super::try_lock_shared();
  }
};

}  // namespace async_coro
