#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

#include <mutex>

namespace async_coro {

template <typename T>
class COTHREAD_SCOPED_CAPABILITY unique_lock : protected std::unique_lock<typename T::super> {
  using super = std::unique_lock<typename T::super>;

 public:
  explicit unique_lock(T& mutex) COTHREAD_ACQUIRE(mutex) : super(mutex) {}
  explicit unique_lock(T& mutex, std::adopt_lock_t) COTHREAD_REQUIRES(mutex) : super(mutex, std::adopt_lock) {}
  explicit unique_lock(T& mutex, std::defer_lock_t) COTHREAD_EXCLUDES(mutex) : super(mutex, std::defer_lock) {}

  unique_lock(unique_lock&&) noexcept = default;
  unique_lock& operator=(unique_lock&&) noexcept = default;

  ~unique_lock() noexcept COTHREAD_RELEASE() = default;

  void lock() COTHREAD_ACQUIRE() { super::lock(); }
  void unlock() COTHREAD_RELEASE() { super::unlock(); }

  bool try_lock() COTHREAD_TRY_ACQUIRE(true) { return super::try_lock(); }

  void swap(unique_lock& other) noexcept {
    super::swap(other);
  }

  using super::owns_lock;

  using super::operator bool;

  auto mutex() const noexcept COTHREAD_RETURN_CAPABILITY(this) {
    return super::mutex();
  }

  friend class condition_variable;
};

template <typename T>
unique_lock(T&) -> unique_lock<T>;

}  // namespace async_coro
