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
  explicit task_handle_awaiter(task_handle<R>&& t_handle) noexcept
      : _th(std::move(t_handle)),
        _on_cancel_callback(on_cancel_callback{this}),
        _on_continue_callback(on_continue_callback{this}) {}

  task_handle_awaiter(const task_handle_awaiter&) = delete;
  task_handle_awaiter(task_handle_awaiter&&) = delete;

  ~task_handle_awaiter() noexcept {
    _can_be_freed.wait(false, std::memory_order::acquire);
  }

  task_handle_awaiter& operator=(const task_handle_awaiter&) = delete;
  task_handle_awaiter& operator=(task_handle_awaiter&&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return _th.done(); }

  template <typename T>
    requires(std::derived_from<T, base_handle>)
  void await_suspend(std::coroutine_handle<T> handle) {
    _can_be_freed.store(false, std::memory_order::relaxed);
    _was_done.store(false, std::memory_order::release);

    _suspension = handle.promise().suspend(2, &_on_cancel_callback);

    _th.continue_with(_on_continue_callback);

    _suspension.try_to_continue_immediately();
  }

  R await_resume() {
    return _th.get();
  }

 private:
  void set_can_be_freed() noexcept {
    if (!_can_be_freed.exchange(true, std::memory_order::release)) {
      _can_be_freed.notify_one();
    }
  }

 private:
  struct on_cancel_callback {
    void operator()() const {
      if (!clb->_was_done.exchange(true, std::memory_order::acquire)) {
        // cancel
        clb->_suspension.try_to_continue_from_any_thread(true);
      }
    }

    task_handle_awaiter* clb;
  };

  class on_continue_callback : public callback<void(promise_result<R>&, bool)> {
    using super = callback<void(promise_result<R>&, bool)>;

   public:
    explicit on_continue_callback(task_handle_awaiter* awaiter) noexcept
        : super(&executor, &deleter),
          _awaiter(awaiter) {}

   private:
    static void executor(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, promise_result<R>& /*result*/, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        clb->set_can_be_freed();
        clb->_suspension.try_to_continue_from_any_thread(cancelled);
      } else {
        clb->set_can_be_freed();
      }
    }

    static void deleter(callback_base* base) noexcept {
      static_cast<on_continue_callback*>(base)->_awaiter->set_can_be_freed();
    }

   private:
    task_handle_awaiter* _awaiter;
  };

 private:
  std::atomic_bool _can_be_freed{true};
  task_handle<R> _th;
  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void()> _on_cancel_callback;
  on_continue_callback _on_continue_callback;
  std::atomic_bool _was_done{false};
};

}  // namespace async_coro::internal
