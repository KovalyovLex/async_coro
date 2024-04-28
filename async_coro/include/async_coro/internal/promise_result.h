#pragma once

#include <async_coro/internal/store_type.h>

#include <utility>

namespace async_coro::internal {
template <typename T>
struct promise_result_base : protected store_type<T> {
  promise_result_base() : is_initialized(false), is_result(false) {}

  ~promise_result_base() noexcept(store_type<T>::nothrow_destructible) {
    if (is_initialized) {
      if (is_result) {
        this->destroy_result();
      } else {
        this->destroy_exception();
      }
    }
  }

  bool has_result() const noexcept { return is_initialized && is_result; }

#if !ASYNC_CORO_NO_EXCEPTIONS
  void unhandled_exception() noexcept {
    ASYNC_CORO_ASSERT(!is_initialized);
    new (&this->exception) std::exception_ptr(std::current_exception());
    is_initialized = true;
    is_result = false;
  }

  void check_exception() const {
    if (is_initialized && !is_result) {
      std::rethrow_exception(this->exception);
    }
  }
#else
  void check_exception() const noexcept {}
#endif

 protected:
  bool is_initialized : 1;
  bool is_result : 1;
};

template <typename T>
struct promise_result : promise_result_base<T> {
  // C++ coroutine api
  template <typename... TArgs>
  void return_value(TArgs&&... args) noexcept {
    ASYNC_CORO_ASSERT(!this->is_initialized);
    this->result.inplace_init(std::forward<TArgs>(args)...);
    this->is_initialized = true;
    this->is_result = true;
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

  auto move_result() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.move();
  }
};

template <>
struct promise_result<void> : promise_result_base<void> {
  // C++ coroutine api
  void return_void() noexcept {
    ASYNC_CORO_ASSERT(!is_initialized);
    is_initialized = true;
    is_result = true;
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
