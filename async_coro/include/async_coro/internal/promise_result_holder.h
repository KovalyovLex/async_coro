#pragma once

#include <async_coro/promise_result.h>

namespace async_coro::internal {

template <typename T>
struct promise_result_holder : async_coro::promise_result<T> {
  // C++ coroutine api
  template <typename... TArgs>
  void return_value(TArgs&&... args) noexcept {
    ASYNC_CORO_ASSERT(!this->_is_initialized);
    this->result.inplace_init(std::forward<TArgs>(args)...);
    this->_is_initialized = true;
    this->_is_result = true;
  }
};

template <>
struct promise_result_holder<void> : async_coro::promise_result<void> {
  // C++ coroutine api
  void return_void() noexcept {
    ASYNC_CORO_ASSERT(!_is_initialized);
    _is_initialized = true;
    _is_result = true;
  }
};

}  // namespace async_coro::internal
