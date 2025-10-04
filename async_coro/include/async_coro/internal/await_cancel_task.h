#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/coroutine_suspender.h>

#include <concepts>

namespace async_coro::internal {

struct await_cancel_task {
  explicit await_cancel_task() noexcept {}

  await_cancel_task(const await_cancel_task&) = delete;
  await_cancel_task(await_cancel_task&&) = delete;

  await_cancel_task& operator=(await_cancel_task&&) = delete;
  await_cancel_task& operator=(const await_cancel_task&) = delete;

  bool await_ready() const noexcept { return false; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    base_handle& promise = h.promise();

    promise.request_cancel();

    auto suspension = promise.suspend(1, nullptr);
    suspension.try_to_continue_immediately();
  }

  void await_resume() noexcept {
    ASYNC_CORO_ASSERT(false);
  }
};

}  // namespace async_coro::internal
