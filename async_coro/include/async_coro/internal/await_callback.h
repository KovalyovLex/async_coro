#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <typename T>
struct await_callback {
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)
      : _on_await(std::forward<T>(callback)),
        _callback(on_cancel_callback{this}) {}

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
    _was_done.store(false, std::memory_order::relaxed);

    _suspension = h.promise().suspend(2, &_callback);

    _on_await([this]() {
      if (!_was_done.exchange(true, std::memory_order::relaxed)) [[likely]] {
        _suspension.try_to_continue_from_any_thread(false);
      }
    });

    _suspension.try_to_continue_immediately();
  }

  ASYNC_CORO_WARNINGS_MSVC_POP

  void await_resume() const noexcept {}

 private:
  struct on_cancel_callback {
    void operator()() const {
      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        // cancel
        clb->_suspension.try_to_continue_from_any_thread(true);
      }
    }

    await_callback* clb;
  };

 private:
  T _on_await;
  coroutine_suspender _suspension;
  concrete_callable_on_stack<on_cancel_callback, void> _callback;
  std::atomic_bool _was_done = false;
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
