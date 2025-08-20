#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/promise_result.h>
#include <async_coro/scheduler.h>

#include <atomic>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <class R>
struct task_handle_awaiter {
  task_handle<R>& _th;

  explicit task_handle_awaiter(task_handle<R>& th) noexcept : _th(th) {}
  task_handle_awaiter(const task_handle_awaiter&) = delete;
  task_handle_awaiter(task_handle_awaiter&&) = delete;
  ~task_handle_awaiter() noexcept = default;

  bool await_ready() const noexcept { return _th.done(); }

  template <typename T>
    requires(std::derived_from<T, base_handle>)
  void await_suspend(std::coroutine_handle<T> h) {
    h.promise().on_suspended();

    _th.continue_with([h](promise_result<R>&) {
      h.promise().continue_execution();
    });
  }

  R await_resume() {
    return _th.get();
  }
};

}  // namespace async_coro::internal