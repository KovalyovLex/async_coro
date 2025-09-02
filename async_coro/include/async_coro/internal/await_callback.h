#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <async_coro/warnings.h>

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <typename T>
struct await_callback {
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)
      : _on_await(std::forward<T>(callback)) {}
  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  bool await_ready() const noexcept { return false; }

  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4702)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    _suspension = h.promise().suspend(2);

    _on_await([done = false, this]() {
      if (!done) [[likely]] {
        const_cast<bool&>(done) = true;  // we intensionally breaks constant here to keep callback immutable

        _suspension.try_to_continue_from_any_thread(false);
      }
    });

    _suspension.try_to_continue_immediately();
  }

  ASYNC_CORO_WARNINGS_MSVC_POP

  void await_resume() const noexcept {}

 private:
  T _on_await;
  coroutine_suspender _suspension;
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
