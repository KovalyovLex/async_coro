#pragma once

#include <async_coro/promise_result.h>

namespace async_coro::internal {

template <typename T>
class promise_result_holder : public async_coro::promise_result<T> {
 public:
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
class promise_result_holder<void> : public async_coro::promise_result<void> {
 public:
  // C++ coroutine api
  void return_void() noexcept {
    ASYNC_CORO_ASSERT(!_is_initialized);
    _is_initialized = true;
    _is_result = true;
  }
};

}  // namespace async_coro::internal
