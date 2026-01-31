#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/promise_result_base.h>
#include <async_coro/internal/store_type.h>

namespace async_coro {

/**
 * @brief Holds the result or exception of a coroutine.
 *
 * This class is used within the coroutine promise type to store either a result
 * of type `T` or an exception.
 *
 * @tparam T The type of the result produced by the coroutine.
 */
template <typename T>
class promise_result : public internal::promise_result_base<T> {
 public:
  // Returns result reference
  auto& get_result_ref() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
    return this->result.get_ref();
  }

  // Returns result const reference
  const auto& get_result_cref() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
    return this->result.get_cref();
  }

  // Moves result
  decltype(auto) move_result() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
    return this->result.move();
  }

  using internal::promise_result_base<T>::has_result;

  using internal::promise_result_base<T>::check_exception;

 protected:
  bool execute_continuation(bool cancelled) override {
    auto continue_callback = this->template release_continuation_functor<void(promise_result<T>&, bool)>();

    if (continue_callback) {
      continue_callback.execute_and_destroy(*this, cancelled);
      return true;
    }
    return false;
  }
};

/**
 * @brief Holds no result or exception of a coroutine.
 *
 * This class is used within the coroutine promise type to store an exception.
 */
template <>
class promise_result<void> : public internal::promise_result_base<void> {
 public:
  // Returns result reference (backward compatibility for void result)
  void get_result_ref() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
  }

  // Returns result const reference (backward compatibility for void result)
  void get_result_cref() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
  }

  // Moves result (backward compatibility for void result)
  void move_result() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT_VARIABLE auto has_result = this->has_result();
    ASYNC_CORO_ASSERT(has_result);
  }

  using internal::promise_result_base<void>::has_result;

  using internal::promise_result_base<void>::check_exception;

 protected:
  bool execute_continuation(bool cancelled) override {
    auto continue_callback = this->release_continuation_functor<void(promise_result<void>&, bool)>();

    if (continue_callback) {
      continue_callback.execute_and_destroy(*this, cancelled);
      return true;
    }
    return false;
  }
};

}  // namespace async_coro
