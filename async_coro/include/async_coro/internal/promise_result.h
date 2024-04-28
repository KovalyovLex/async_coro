#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/store_type.h>

#include <utility>

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

  void check_exception() const noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
#if !ASYNC_CORO_NO_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
    if (_is_initialized && !_is_result) {
      std::rethrow_exception(this->exception);
    }
#endif
  }
};

template <typename T>
struct promise_result : promise_result_base<T> {
  // C++ coroutine api
  template <typename... TArgs>
  void return_value(TArgs&&... args) noexcept {
    ASYNC_CORO_ASSERT(!this->_is_initialized);
    this->result.inplace_init(std::forward<TArgs>(args)...);
    this->_is_initialized = true;
    this->_is_result = true;
  }

  auto& get_result_ref() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.get_ref();
  }

  const auto& get_result_cref() const noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.get_cref();
  }

  decltype(auto) move_result() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.move();
  }
};

template <>
struct promise_result<void> : promise_result_base<void> {
  // C++ coroutine api
  void return_void() noexcept {
    ASYNC_CORO_ASSERT(!_is_initialized);
    _is_initialized = true;
    _is_result = true;
  }

  void get_result_ref() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
  }

  void get_result_cref() const noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
  }

  void move_result() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
  }
};
}  // namespace async_coro::internal
