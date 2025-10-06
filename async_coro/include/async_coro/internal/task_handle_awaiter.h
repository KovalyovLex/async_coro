#pragma once

#include <async_coro/callback.h>
#include <async_coro/coroutine_suspender.h>
#include <async_coro/promise_result.h>

#include <atomic>

namespace async_coro {

template <class R>
class task_handle;

class base_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <class R>
class task_handle_awaiter {
 public:
  explicit task_handle_awaiter(task_handle<R>&& th) noexcept
      : _th(std::move(th)),
        _on_cancel_callback(on_cancel_callback{*this}),
        _on_continue_callback(on_continue_callback{*this}) {}

  task_handle_awaiter(const task_handle_awaiter&) = delete;
  task_handle_awaiter(task_handle_awaiter&&) = delete;
  ~task_handle_awaiter() noexcept = default;

  bool await_ready() const noexcept { return _th.done(); }

  template <typename T>
    requires(std::derived_from<T, base_handle>)
  void await_suspend(std::coroutine_handle<T> h) {
    _was_done.store(false, std::memory_order::relaxed);

    _suspension = h.promise().suspend(2, &_on_cancel_callback);

    _th.continue_with(_on_continue_callback);

    _suspension.try_to_continue_immediately();
  }

  R await_resume() {
    return _th.get();
  }

 private:
  struct on_cancel_callback {
    void operator()() const {
      if (!clb._was_done.exchange(true, std::memory_order::relaxed)) {
        // cancel
        clb._suspension.try_to_continue_from_any_thread(true);
      }
    }

    task_handle_awaiter& clb;
  };

  struct on_continue_callback {
    void operator()(promise_result<R>&, bool canceled) const {
      if (!clb._was_done.exchange(true, std::memory_order::relaxed)) {
        clb._suspension.try_to_continue_from_any_thread(canceled);
      }
    }

    task_handle_awaiter& clb;
  };

 private:
  task_handle<R> _th;
  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void> _on_cancel_callback;
  callback_on_stack<on_continue_callback, void, promise_result<R>&, bool> _on_continue_callback;
  std::atomic_bool _was_done{false};
};

}  // namespace async_coro::internal
