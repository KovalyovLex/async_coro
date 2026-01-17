#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/coroutine_suspender.h>

#include <concepts>

namespace async_coro::internal {

class await_cancel_task {
 public:
  explicit await_cancel_task() noexcept = default;

  await_cancel_task(const await_cancel_task&) = delete;
  await_cancel_task(await_cancel_task&&) = delete;

  ~await_cancel_task() noexcept = default;

  await_cancel_task& operator=(await_cancel_task&&) = delete;
  await_cancel_task& operator=(const await_cancel_task&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return false; }  // NOLINT(*static)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    base_handle& promise = handle.promise();

    auto suspension = promise.suspend(1, nullptr);

    promise.request_cancel();

    suspension.try_to_continue_immediately();
  }

  void await_resume() noexcept {  // NOLINT(*static)
    ASYNC_CORO_ASSERT(false);     // NOLINT(*static-assert)
  }
};

}  // namespace async_coro::internal
