#pragma once

#include <async_coro/base_handle.h>
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
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.get_ref();
  }

  // Returns result const reference
  const auto& get_result_cref() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.get_cref();
  }

  // Moves result
  decltype(auto) move_result() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
    return this->result.move();
  }

  using internal::promise_result_base<T>::has_result;

  using internal::promise_result_base<T>::check_exception;

 protected:
  bool execute_continuation(bool cancelled) override {
    if (auto* continue_with = static_cast<callback<void, promise_result<T>&, bool>*>(this->release_continuation_functor())) {
      continue_with->execute(*this, cancelled);
      continue_with->destroy();
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
    ASYNC_CORO_ASSERT(this->has_result());
  }

  // Returns result const reference (backward compatibility for void result)
  void get_result_cref() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
  }

  // Moves result (backward compatibility for void result)
  void move_result() noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    this->check_exception();
    ASYNC_CORO_ASSERT(this->has_result());
  }

  using internal::promise_result_base<void>::has_result;

  using internal::promise_result_base<void>::check_exception;

 protected:
  bool execute_continuation(bool cancelled) override {
    callback<void, promise_result<void>&, bool>::ptr continue_with{static_cast<callback<void, promise_result<void>&, bool>*>(this->release_continuation_functor())};
    if (continue_with) {
      continue_with->execute(*this, cancelled);
      return true;
    }
    return false;
  }
};

}  // namespace async_coro
