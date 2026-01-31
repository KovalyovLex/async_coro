#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/coroutine_suspender.h>
#include <async_coro/utils/callback_on_stack.h>

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
      : _awaiter(std::move(awaiter)) {
  }

  await_suspension_wrapper(const await_suspension_wrapper&) = delete;
  await_suspension_wrapper(await_suspension_wrapper&& other) noexcept
      : _awaiter(std::move(other._awaiter)) {
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
    base_handle& coro_handle = handle.promise();
    _suspension = coro_handle.suspend(3, _cancel_callback.get_ptr());

    _awaiter.adv_await_suspend(_continue_callback.get_ptr(), coro_handle);

    _suspension.try_to_continue_immediately();
  }

  auto await_resume() {
    return _awaiter.adv_await_resume();
  }

 private:
  class cancel_callback : public callback_on_stack<cancel_callback, base_handle::cancel_callback> {
   public:
    void on_destroy() {
      auto& awaiter = this->get_owner(&await_suspension_wrapper::_cancel_callback);

      awaiter._suspension.try_to_continue_from_any_thread(false);
    }

    void on_execute_and_destroy() {
      auto& awaiter = this->get_owner(&await_suspension_wrapper::_cancel_callback);

      // cancel awaiting first
      awaiter._awaiter.cancel_adv_await();

      // then decrease num suspensions
      awaiter._suspension.try_to_continue_from_any_thread(true);
    }
  };

  class continue_callback : public callback_on_stack<continue_callback, internal::continue_callback> {
   public:
    void on_destroy() {
      auto& awaiter = this->get_owner(&await_suspension_wrapper::_continue_callback);

      awaiter._suspension.try_to_continue_from_any_thread(false);
    }

    internal::continue_callback::return_type on_execute_and_destroy(bool cancelled) {
      auto& awaiter = this->get_owner(&await_suspension_wrapper::_continue_callback);

      // remove cancel callback
      awaiter._suspension.remove_cancel_callback();

      // then decrease num suspensions
      awaiter._suspension.try_to_continue_from_any_thread(cancelled);

      return {continue_callback_holder{nullptr}, false};
    }
  };

 private:
  coroutine_suspender _suspension;
  cancel_callback _cancel_callback;
  continue_callback _continue_callback;
  TAwaiter _awaiter;
};

}  // namespace async_coro::internal
