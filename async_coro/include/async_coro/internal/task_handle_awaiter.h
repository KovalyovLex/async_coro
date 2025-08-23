#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/coroutine_suspender.h>
#include <async_coro/promise_result.h>
#include <async_coro/scheduler.h>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <class R>
struct task_handle_awaiter {
  explicit task_handle_awaiter(task_handle<R>& th) noexcept : _th(th) {}
  task_handle_awaiter(const task_handle_awaiter&) = delete;
  task_handle_awaiter(task_handle_awaiter&&) = delete;
  ~task_handle_awaiter() noexcept = default;

  bool await_ready() const noexcept { return _th.done(); }

  template <typename T>
    requires(std::derived_from<T, base_handle>)
  void await_suspend(std::coroutine_handle<T> h) {
    _suspension = h.promise().suspend(2);

    _th.continue_with([this](promise_result<R>&) {
      _suspension.try_to_continue_from_any_thread();
    });

    _suspension.try_to_continue_immediately();
  }

  R await_resume() {
    return _th.get();
  }

 private:
  task_handle<R>& _th;
  coroutine_suspender _suspension;
};

}  // namespace async_coro::internal