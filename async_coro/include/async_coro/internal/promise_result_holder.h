#pragma once

#include <async_coro/promise_result.h>

#include <type_traits>

namespace async_coro::internal {

template <typename T>
class promise_result_holder : public async_coro::promise_result<T> {
 public:
  // C++ coroutine api
  template <typename... TArgs>
    requires(std::is_constructible_v<T, TArgs...>)
  void return_value(TArgs&&... args) noexcept(std::is_nothrow_constructible_v<T, TArgs...>) {
    ASYNC_CORO_ASSERT(!this->is_initialized());

    this->result.inplace_init(std::forward<TArgs>(args)...);
    this->set_initialized(true);
  }
};

template <>
class promise_result_holder<void> : public async_coro::promise_result<void> {
 public:
  // C++ coroutine api
  void return_void() noexcept {
    ASYNC_CORO_ASSERT(!is_initialized());

    set_initialized(true);
  }
};

}  // namespace async_coro::internal
