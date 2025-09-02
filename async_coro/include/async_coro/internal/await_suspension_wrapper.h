#pragma once

#include <async_coro/coroutine_suspender.h>

#include <concepts>
#include <coroutine>

namespace async_coro {
class base_handle;
}

namespace async_coro::internal {

template <class TAwaiter>
struct await_suspension_wrapper {
  bool await_ready() noexcept {
    return _awaiter.await_ready();
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    _suspension = h.promise().suspend(2);

    _awaiter.continue_after_complete([this](bool canceled) {
      _suspension.try_to_continue_from_any_thread(canceled);
    });

    _suspension.try_to_continue_immediately();
  }

  auto await_resume() {
    return _awaiter.await_resume();
  }

  TAwaiter _awaiter;
  coroutine_suspender _suspension{};
};

}  // namespace async_coro::internal