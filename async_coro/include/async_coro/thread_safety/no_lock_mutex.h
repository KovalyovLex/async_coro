#pragma once

#include <async_coro/thread_safety/analysis.h>

namespace async_coro {

// Fake mutex for thread safety analysis
class CORO_THREAD_CAPABILITY("mutex") no_lock_mutex {
 public:
  using super = no_lock_mutex;

  no_lock_mutex() = default;
  no_lock_mutex(const no_lock_mutex&) = delete;
  no_lock_mutex(no_lock_mutex&&) = delete;

  ~no_lock_mutex() noexcept = default;

  no_lock_mutex& operator=(const no_lock_mutex&) = delete;
  no_lock_mutex& operator=(no_lock_mutex&&) = delete;

  constexpr void lock() CORO_THREAD_ACQUIRE() {
  }

  [[nodiscard]] constexpr bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {  // NOLINT(*-static)
    return true;
  }

  constexpr void unlock() noexcept CORO_THREAD_RELEASE() {
  }
};

}  // namespace async_coro
