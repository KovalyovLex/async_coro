#pragma once

#include <async_coro/config.h>

#include <atomic>
#include <cstdint>

namespace async_coro {

// Provides effective sleep\notify functionality for thread without spurious wake ups
class thread_notifier {
 public:
  thread_notifier() noexcept = default;

  thread_notifier(const thread_notifier&) = delete;
  thread_notifier(thread_notifier&&) = delete;
  ~thread_notifier() noexcept = default;

  thread_notifier& operator=(const thread_notifier&) = delete;
  thread_notifier& operator=(thread_notifier&&) = delete;

  // Notifies sleeping thread or will force to skip next sleep of the thread.
  // Returns true if sleeping thread was awaken.
  bool notify() noexcept {
    if (_state.exchange(state_signalled, std::memory_order::relaxed) == state_sleeping) {
      _state.notify_one();
      return true;
    }
    return false;
  }

  // Puts current thread in sleep until receive notification.
  // If we got notification after reset_notification but before sleep, this sleep will be ignored.
  void sleep() noexcept {
    auto expected = state_idle;
    if (_state.compare_exchange_strong(expected, state_sleeping, std::memory_order::relaxed)) {
      _state.wait(state_sleeping, std::memory_order::relaxed);

      reset_notification();
    } else if (expected == state_signalled) {
      reset_notification();
    } else {
      ASYNC_CORO_ASSERT(false && "Unexpected state");  // NOLINT(*-static-assert)
    }
  }

  // Can be called by owning thread (who calls sleep) to reset any previous notifications.
  void reset_notification() noexcept {
    ASYNC_CORO_ASSERT(_state.load(std::memory_order::relaxed) != state_sleeping);

    // No real acq_rel synchronization of data, just to prevent any reordering
    _state.store(state_idle, std::memory_order::release);
  }

 private:
  static constexpr uint8_t state_idle = 0;
  static constexpr uint8_t state_sleeping = 1;
  static constexpr uint8_t state_signalled = 2;

  std::atomic<uint8_t> _state = state_idle;

  static_assert(std::atomic<uint8_t>::is_always_lock_free, "Wrong platform");
};

}  // namespace async_coro
