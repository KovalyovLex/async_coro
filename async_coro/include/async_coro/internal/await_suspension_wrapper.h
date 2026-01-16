#pragma once

#include <async_coro/callback.h>
#include <async_coro/internal/advanced_awaiter.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/coroutine_suspender.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>

namespace async_coro {
class base_handle;
}

namespace async_coro::internal {

template <class TAwaiter>
  requires advanced_awaiter<TAwaiter>
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
    return _awaiter.await_ready();
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    _was_done.store(false, std::memory_order::release);

    _suspension = handle.promise().suspend(2, &_cancel_callback);

    _awaiter.continue_after_complete(_continue_callback);

    _suspension.try_to_continue_immediately();
  }

  auto await_resume() {
    return _awaiter.await_resume();
  }

 private:
  struct on_cancel_callback {
    void operator()() const {
      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        // cancel
        clb->_awaiter.cancel_await();

        clb->_suspension.try_to_continue_from_any_thread(true);
      }
    }

    await_suspension_wrapper* clb;
  };

  class on_continue_callback : public continue_callback {
    using super = continue_callback;

   public:
    explicit on_continue_callback(await_suspension_wrapper* awaiter) noexcept
        : super(&executor, nullptr),
          _awaiter(awaiter) {}

   private:
    static super::return_type executor(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        clb->_suspension.try_to_continue_from_any_thread(cancelled);
      }

      return {nullptr, false};
    }

   private:
    await_suspension_wrapper* _awaiter;
  };

 private:
  TAwaiter _awaiter;
  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void()> _cancel_callback;
  on_continue_callback _continue_callback;
  std::atomic_bool _was_done{false};
};

}  // namespace async_coro::internal
