#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/store_type.h>

namespace async_coro::internal {

template <typename T>
class promise_result_base : public base_handle, protected store_type<T> {
  static_assert(store_type<T>::nothrow_destructible, "T should be noexcept destructible to be able to return it as result");

 public:
  promise_result_base() noexcept = default;

  promise_result_base(const promise_result_base&) = delete;
  promise_result_base(promise_result_base&&) = delete;

  ~promise_result_base() noexcept override {
    if (_is_initialized) {
      if (_is_result) {
        this->destroy_result();
      } else {
        this->destroy_exception();
      }
    }
  }

  promise_result_base& operator=(const promise_result_base&) = delete;
  promise_result_base& operator=(promise_result_base&&) = delete;

  // Checks has result
  [[nodiscard]] bool has_result() const noexcept { return _is_initialized && _is_result; }

#if ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  void unhandled_exception() noexcept {
    ASYNC_CORO_ASSERT(!_is_initialized);
#if ASYNC_CORO_WITH_EXCEPTIONS
    new (&this->exception) std::exception_ptr(std::current_exception());
#else
    ASYNC_CORO_ASSERT(false);  // NOLINT(*static-assert)
#endif
    _is_initialized = true;
    _is_result = false;
  }
#endif

  // If exception was caught in coroutine rethrows it
  void check_exception() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
#if ASYNC_CORO_WITH_EXCEPTIONS
    if (_is_initialized && !_is_result) [[unlikely]] {
      std::rethrow_exception(this->exception);
    }
#endif
  }

#if ASYNC_CORO_WITH_EXCEPTIONS
  void check_exception_base() override {
    check_exception();
  }
#endif
};

}  // namespace async_coro::internal
