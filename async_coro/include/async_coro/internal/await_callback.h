#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <atomic>
#include <concepts>
#include <coroutine>

namespace async_coro::internal {

template <typename T>
struct await_callback {
  explicit await_callback(T&& callback)
      : on_await(std::move(callback)) {}
  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  bool await_ready() const noexcept { return false; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    h.promise().on_suspended();

    on_await([h, done = false, this]() mutable {
      if (!done) [[likely]] {
        done = true;

        base_handle& handle = h.promise();
        if (suspended.load(std::memory_order::acquire)) {
          handle.get_scheduler().continue_execution(handle);
        } else {
          handle.get_scheduler().plan_continue_execution(handle);
        }
      }
    });

    suspended.store(true, std::memory_order::release);
  }

  void await_resume() const noexcept {}

  T on_await;
  std::atomic_bool suspended{false};
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
