#pragma once

#include <async_coro/config.h>

#include <atomic>
#include <cstdint>
#include <utility>

namespace async_coro {

class base_handle;

/// @brief Manages the suspension and resumption of a coroutine based on a suspend count.
///
/// The `coroutine_suspender` class is responsible for controlling the suspension state of a coroutine.
/// It tracks the number of outstanding suspensions and resumes the coroutine when the suspend count reaches zero.
/// This class is move-only and not copyable.
///
/// @note The suspend count is managed atomically. The coroutine is resumed either immediately or scheduled
/// for execution on any thread, depending on which method is called. Method `try_to_continue_immediately` should be called at least once.
///
/// @see try_to_continue_from_any_thread
/// @see try_to_continue_immediately
class coroutine_suspender {
 public:
  coroutine_suspender() noexcept {}
  coroutine_suspender(base_handle& handle, std::uint32_t suspend_count) noexcept
      : _handle(&handle),
        _suspend_count(suspend_count) {
    // try_to_continue_on_same_thread should be called at least once
    ASYNC_CORO_ASSERT(suspend_count > 0);
  }

  coroutine_suspender(const coroutine_suspender&) = delete;
  coroutine_suspender(coroutine_suspender&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)),
        _suspend_count(other._suspend_count.exchange(0, std::memory_order::relaxed)) {
  }

  coroutine_suspender& operator=(const coroutine_suspender&) = delete;

  coroutine_suspender& operator=(coroutine_suspender&& other) noexcept {
    ASYNC_CORO_ASSERT(_suspend_count.load(std::memory_order::relaxed) == 0);

    _handle = std::exchange(other._handle, nullptr);
    _suspend_count.store(other._suspend_count.exchange(0, std::memory_order::relaxed), std::memory_order::relaxed);

    return *this;
  }

  ~coroutine_suspender() noexcept = default;

  /// Decrements suspend_count and if it zero schedules coroutine.
  void try_to_continue_from_any_thread();

  /// Decrements suspend_count and if it zero schedules coroutine.
  ///
  /// @note Should be called only on the thread where this object was created.
  /// This method should be called at least once.
  void try_to_continue_immediately();

 private:
  base_handle* _handle = nullptr;
  std::atomic<std::uint32_t> _suspend_count{0};
  bool _was_continued_immediately{false};
};

}  // namespace async_coro