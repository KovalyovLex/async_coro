#pragma once

#include <async_coro/callback.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/coroutine_suspender.h>

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace async_coro {
class base_handle;
}

namespace async_coro::internal {

template <advanced_awaitable TAwaiter>
class await_suspension_wrapper {
 public:
  explicit await_suspension_wrapper(TAwaiter&& awaiter) noexcept(std::is_nothrow_constructible_v<TAwaiter, TAwaiter&&>)
      : _awaiter(std::move(awaiter)),
        _cancel_callback(on_cancel_callback{this}),
        _continue_callback(on_continue_callback{this}) {
  }

  await_suspension_wrapper(const await_suspension_wrapper&) = delete;
  await_suspension_wrapper(await_suspension_wrapper&& other) noexcept
      : _awaiter(std::move(other._awaiter)),
        _cancel_callback(on_cancel_callback{this}),
        _continue_callback(on_continue_callback{this}) {
  }

  ~await_suspension_wrapper() noexcept = default;

  await_suspension_wrapper& operator=(const await_suspension_wrapper&) = delete;
  await_suspension_wrapper& operator=(await_suspension_wrapper&&) = delete;

  bool await_ready() noexcept {
    return _awaiter.adv_await_ready();
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    // cancel and continue should both be called in any case
    _suspension = handle.promise().suspend(3, &_cancel_callback);

    _awaiter.adv_await_suspend(_continue_callback);

    _suspension.try_to_continue_immediately();
  }

  auto await_resume() {
    return _awaiter.adv_await_resume();
  }

 private:
  class on_cancel_callback : public callback<void()> {
    using super = callback<void()>;

   public:
    explicit on_cancel_callback(await_suspension_wrapper* awaiter) noexcept
        : super(&on_execute, &on_destroy),
          _awaiter(awaiter) {}

   private:
    static void on_execute(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_cancel_callback*>(base)->_awaiter;

      // cancel awaiting first
      clb->_awaiter.cancel_adv_await();

      // then decrease num suspensions
      clb->_suspension.try_to_continue_from_any_thread(true);
    }

    static void on_destroy(callback_base* base) noexcept {
      auto* clb = static_cast<on_cancel_callback*>(base)->_awaiter;

      clb->_suspension.try_to_continue_from_any_thread(false);
    }

   private:
    await_suspension_wrapper* _awaiter;
  };

  class on_continue_callback : public continue_callback {
    using super = continue_callback;

   public:
    explicit on_continue_callback(await_suspension_wrapper* awaiter) noexcept
        : super(&on_execute, &on_destroy),
          _awaiter(awaiter) {}

   private:
    static super::return_type on_execute(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      // destroying cancel callback first to dec num suspensions
      clb->_suspension.remove_cancel_callback();

      // continue execution
      clb->_suspension.try_to_continue_from_any_thread(cancelled);

      return {nullptr, false};
    }

    static void on_destroy(callback_base* base) noexcept {
      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      clb->_suspension.try_to_continue_from_any_thread(false);
    }

   private:
    await_suspension_wrapper* _awaiter;
  };

 private:
  TAwaiter _awaiter;
  coroutine_suspender _suspension;
  on_cancel_callback _cancel_callback;
  on_continue_callback _continue_callback;
};

}  // namespace async_coro::internal
