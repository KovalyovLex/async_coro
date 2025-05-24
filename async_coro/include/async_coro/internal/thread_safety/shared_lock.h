#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

#include <shared_mutex>

namespace async_coro {

template <typename T>
class COTHREAD_SCOPED_CAPABILITY shared_lock : protected std::shared_lock<typename T::super> {
  using super = std::shared_lock<typename T::super>;

 public:
  explicit shared_lock(T& mutex) COTHREAD_ACQUIRE_SHARED(mutex) : super(mutex) {}
  explicit shared_lock(T& mutex, std::adopt_lock_t) COTHREAD_REQUIRES_SHARED(mutex) : super(mutex, std::adopt_lock) {}
  explicit shared_lock(T& mutex, std::defer_lock_t) COTHREAD_EXCLUDES(mutex) : super(mutex, std::defer_lock) {}

  shared_lock(shared_lock&&) noexcept = default;
  shared_lock& operator=(shared_lock&&) noexcept = default;

  ~shared_lock() noexcept COTHREAD_RELEASE_SHARED() = default;

  void lock() COTHREAD_ACQUIRE_SHARED() { super::lock(); }
  void unlock() COTHREAD_RELEASE_SHARED() { super::unlock(); }

  bool try_lock() COTHREAD_TRY_ACQUIRE_SHARED(true) { return super::try_lock(); }

  void swap(shared_lock& other) noexcept {
    super::swap(other);
  }

  using super::owns_lock;

  using super::operator bool;

  auto mutex() const noexcept COTHREAD_RETURN_CAPABILITY(this) {
    return super::mutex();
  }
};

template <typename T>
shared_lock(T&) -> shared_lock<T>;

}  // namespace async_coro
