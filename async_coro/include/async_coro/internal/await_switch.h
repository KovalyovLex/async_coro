#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {

struct await_switch {
  explicit await_switch(execution_queue_mark queue) noexcept : execution_queue(queue) {}
  await_switch(const await_switch&) = delete;
  await_switch(await_switch&&) = delete;

  await_switch& operator=(await_switch&&) = delete;
  await_switch& operator=(const await_switch&) = delete;

  bool await_ready() const noexcept { return !need_switch; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    ASYNC_CORO_ASSERT(need_switch);

    base_handle& handle = h.promise();
    handle.switch_execution_queue(execution_queue);
  }

  void await_resume() const noexcept {}

  await_switch& coro_await_transform(base_handle& parent) noexcept {
    need_switch = parent.get_execution_queue() != execution_queue;
    return *this;
  }

  execution_queue_mark execution_queue;
  bool need_switch = true;
};

}  // namespace async_coro::internal
