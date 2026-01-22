#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/scheduler.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {

struct await_switch {
  explicit await_switch(execution_queue_mark queue) noexcept : _execution_queue(queue) {}
  await_switch(const await_switch&) = delete;
  await_switch(await_switch&&) = delete;

  ~await_switch() noexcept = default;

  await_switch& operator=(await_switch&&) = delete;
  await_switch& operator=(const await_switch&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return !_need_switch; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    ASYNC_CORO_ASSERT(_need_switch);

    base_handle& promise = handle.promise();
    _queue_before = promise.get_execution_queue();
    promise.switch_execution_queue(_execution_queue);
  }

  auto await_resume() const noexcept { return _queue_before; }  // NOLINT(*nodiscard*)

  await_switch& coro_await_transform(base_handle& parent) noexcept {
    _need_switch = parent.get_execution_queue() != _execution_queue;
    return *this;
  }

 private:
  execution_queue_mark _execution_queue;
  execution_queue_mark _queue_before = execution_queues::any;
  bool _need_switch = true;
};

}  // namespace async_coro::internal
