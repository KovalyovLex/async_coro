#pragma once

#include <async_coro/internal/thread_safety/analysis.h>

namespace async_coro {

// Fake mutex for thread safety analysis
class COTHREAD_CAPABILITY("mutex") no_lock_mutex {
 public:
  using super = no_lock_mutex;

  no_lock_mutex() = default;
  no_lock_mutex(const no_lock_mutex&) = delete;
  no_lock_mutex& operator=(const no_lock_mutex&) = delete;

  constexpr void lock() COTHREAD_ACQUIRE() {
  }

  [[nodiscard]] constexpr bool try_lock() noexcept COTHREAD_TRY_ACQUIRE(true) {
    return true;
  }

  constexpr void unlock() noexcept COTHREAD_RELEASE() {
  }
};

}  // namespace async_coro
