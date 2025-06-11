#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {

template <typename T>
struct await_callback {
  T _on_await;

  explicit await_callback(T&& on_await)
      : _on_await(std::move(on_await)) {}
  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  bool await_ready() const noexcept { return false; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    h.promise().on_suspended();

    _on_await([h, executed = false]() mutable {
      if (!executed) [[likely]] {
        executed = true;

        base_handle& handle = h.promise();
        handle.get_scheduler().continue_execution(handle);
      }
    });
  }

  void await_resume() const noexcept {}
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
