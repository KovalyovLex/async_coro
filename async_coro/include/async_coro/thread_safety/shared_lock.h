#pragma once

#include <async_coro/thread_safety/analysis.h>

#include <shared_mutex>

namespace async_coro {

template <typename T>
class CORO_THREAD_SCOPED_CAPABILITY shared_lock : protected std::shared_lock<typename T::super> {
  using super = std::shared_lock<typename T::super>;

 public:
  explicit shared_lock(T& mutex) CORO_THREAD_ACQUIRE_SHARED(mutex) : super(mutex) {}
  explicit shared_lock(T& mutex, std::adopt_lock_t /*tag*/) CORO_THREAD_REQUIRES_SHARED(mutex) : super(mutex, std::adopt_lock) {}
  explicit shared_lock(T& mutex, std::defer_lock_t /*tag*/) CORO_THREAD_EXCLUDES(mutex) : super(mutex, std::defer_lock) {}

  shared_lock(const shared_lock&) noexcept = delete;
  shared_lock(shared_lock&&) noexcept = default;

  ~shared_lock() noexcept CORO_THREAD_RELEASE_SHARED() = default;

  shared_lock& operator=(shared_lock&&) noexcept = default;
  shared_lock& operator=(const shared_lock&) noexcept = delete;

  void lock() CORO_THREAD_ACQUIRE_SHARED() { super::lock(); }
  void unlock() CORO_THREAD_RELEASE_SHARED() { super::unlock(); }

  bool try_lock() CORO_THREAD_TRY_ACQUIRE_SHARED(true) { return super::try_lock(); }

  void swap(shared_lock& other) noexcept {
    super::swap(other);
  }

  using super::owns_lock;

  using super::operator bool;

  auto mutex() const noexcept CORO_THREAD_RETURN_CAPABILITY(this) {
    return super::mutex();
  }
};

template <typename T>
shared_lock(T&) -> shared_lock<T>;

}  // namespace async_coro
