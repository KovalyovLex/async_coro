#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/store_type.h>

namespace async_coro::internal {

template <typename T>
struct promise_result_base : base_handle, protected store_type<T> {
  promise_result_base() noexcept = default;

  ~promise_result_base() noexcept(store_type<T>::nothrow_destructible) {
    if (_is_initialized) {
      if (_is_result) {
        this->destroy_result();
      } else {
        this->destroy_exception();
      }
    }
  }

  // Checks has result
  bool has_result() const noexcept { return _is_initialized && _is_result; }

#if ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  void unhandled_exception() noexcept {
    ASYNC_CORO_ASSERT(!_is_initialized);
#if !ASYNC_CORO_NO_EXCEPTIONS
    new (&this->exception) std::exception_ptr(std::current_exception());
#endif
    _is_initialized = true;
    _is_result = false;
  }
#endif

  // If exception was caught in coroutine rethrows it
  void check_exception() const noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
#if !ASYNC_CORO_NO_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
    if (_is_initialized && !_is_result) {
      std::rethrow_exception(this->exception);
    }
#endif
  }
};

}  // namespace async_coro::internal
