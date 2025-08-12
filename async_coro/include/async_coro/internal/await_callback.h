#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <utility>

namespace async_coro::internal {

template <typename T>
struct await_callback {
  explicit await_callback(T&& callback)
      : _on_await(std::forward<T>(callback)) {}
  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  bool await_ready() const noexcept { return false; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    ASYNC_CORO_ASSERT(_suspended.load(std::memory_order::relaxed) == false);

    h.promise().on_suspended();

    _on_await([h, done = false, this]() {
      if (!done && !_was_continued.exchange(true, std::memory_order::relaxed)) [[likely]] {
        const_cast<bool&>(done) = true;  // we intensionally breaks constant here to keep callback immutable

        base_handle& handle = h.promise();
        if (_suspended.load(std::memory_order::acquire)) {
          handle.get_scheduler().continue_execution(handle);
        } else {
          handle.get_scheduler().plan_continue_execution(handle);
        }
      }
    });

    _suspended.store(true, std::memory_order::release);
  }

  void await_resume() const noexcept {}

 private:
  T _on_await;
  std::atomic_bool _suspended{false};
  std::atomic_bool _was_continued{false};
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
