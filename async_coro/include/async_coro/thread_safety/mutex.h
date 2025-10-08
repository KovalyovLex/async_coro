#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <mutex>

namespace async_coro {

template <typename T>
class unique_lock;

// Replacement for std::mutex to work with clang thread safety analysis
class CORO_THREAD_CAPABILITY("mutex") mutex : protected std::mutex {
  friend unique_lock<mutex>;

 public:
  using super = std::mutex;

  mutex() = default;
  mutex(const mutex&) = delete;
  mutex(mutex&&) = delete;

  ~mutex() noexcept = default;

  mutex& operator=(const mutex&) = delete;
  mutex& operator=(mutex&&) = delete;

  void lock() CORO_THREAD_ACQUIRE() {
    super::lock();
  }

  [[nodiscard]] bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {
    return super::try_lock();
  }

  void unlock() noexcept CORO_THREAD_RELEASE() {
    super::unlock();
  }
};

}  // namespace async_coro
