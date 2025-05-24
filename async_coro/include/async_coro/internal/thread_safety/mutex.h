#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

#include <mutex>

namespace async_coro {

template <typename T>
class unique_lock;

// Replacement for std::mutex to work with clang thread safety analysis
class COTHREAD_CAPABILITY("mutex") mutex : protected std::mutex {
  friend unique_lock<mutex>;

 public:
  using super = std::mutex;

  mutex() = default;
  mutex(const mutex&) = delete;
  mutex& operator=(const mutex&) = delete;

  void lock() COTHREAD_ACQUIRE() {
    super::lock();
  }

  [[nodiscard]] bool try_lock() noexcept COTHREAD_TRY_ACQUIRE(true) {
    return super::try_lock();
  }

  void unlock() noexcept COTHREAD_RELEASE() {
    super::unlock();
  }
};

}  // namespace async_coro
