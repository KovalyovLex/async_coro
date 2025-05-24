#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

namespace async_coro {

// Fake mutex for thread safety analysis
class COTHREAD_CAPABILITY("mutex") no_lock_shared_mutex {
 public:
  using super = no_lock_shared_mutex;

  no_lock_shared_mutex() = default;
  no_lock_shared_mutex(const no_lock_shared_mutex&) = delete;
  no_lock_shared_mutex& operator=(const no_lock_shared_mutex&) = delete;

  constexpr void lock() COTHREAD_ACQUIRE() {
  }

  [[nodiscard]] constexpr bool try_lock() noexcept COTHREAD_TRY_ACQUIRE(true) {
    return true;
  }

  constexpr void unlock() noexcept COTHREAD_RELEASE() {
  }

  constexpr void lock_shared() noexcept COTHREAD_ACQUIRE_SHARED() {
  }

  constexpr void unlock_shared() noexcept COTHREAD_RELEASE_SHARED() {
  }

  constexpr bool try_lock_shared() noexcept COTHREAD_TRY_ACQUIRE_SHARED(true) {
    return true;
  }
};

}  // namespace async_coro
