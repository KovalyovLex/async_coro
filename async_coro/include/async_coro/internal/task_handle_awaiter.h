#pragma once

#include <async_coro/callback.h>
#include <async_coro/internal/coroutine_suspender.h>
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

  ~task_handle_awaiter() noexcept = default;

  task_handle_awaiter& operator=(const task_handle_awaiter&) = delete;
  task_handle_awaiter& operator=(task_handle_awaiter&&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return _th.done(); }

  template <typename T>
    requires(std::derived_from<T, base_handle>)
  void await_suspend(std::coroutine_handle<T> handle) {
    // cancel and continue should call continue in case of execution or destroy
    _suspension = handle.promise().suspend(3, &_on_cancel_callback);

    _th.continue_with(_on_continue_callback);

    _suspension.try_to_continue_immediately();
  }

  R await_resume() {
    return _th.get();
  }

 private:
  class on_cancel_callback : public callback<void()> {
    using super = callback<void()>;

   public:
    explicit on_cancel_callback(task_handle_awaiter* awaiter) noexcept
        : super(&on_execute, nullptr),
          _awaiter(awaiter) {}

   private:
    static void on_execute(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_cancel_callback*>(base)->_awaiter;

      // cancel task that we await
      clb->_th.request_cancel();
      clb->_th.reset_continue();

      // cancel
      clb->_suspension.try_to_continue_from_any_thread(true);
    }

    static void on_destroy(callback_base* base) noexcept {
      auto* clb = static_cast<on_cancel_callback*>(base)->_awaiter;

      clb->_suspension.try_to_continue_from_any_thread(false);
    }

   private:
    task_handle_awaiter* _awaiter;
  };

  class on_continue_callback : public callback<void(promise_result<R>&, bool)> {
    using super = callback<void(promise_result<R>&, bool)>;

   public:
    explicit on_continue_callback(task_handle_awaiter* awaiter) noexcept
        : super(&on_execute, &on_destroy),
          _awaiter(awaiter) {}

   private:
    static void on_execute(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, promise_result<R>& /*result*/, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      clb->_suspension.try_to_continue_from_any_thread(cancelled);
    }

    static void on_destroy(callback_base* base) noexcept {
      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      clb->_suspension.try_to_continue_from_any_thread(false);
    }

   private:
    task_handle_awaiter* _awaiter;
  };

 private:
  task_handle<R> _th;
  coroutine_suspender _suspension;
  on_cancel_callback _on_cancel_callback;
  on_continue_callback _on_continue_callback;
};

}  // namespace async_coro::internal
