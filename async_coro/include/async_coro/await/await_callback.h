#pragma once

#include <async_coro/internal/await_callback.h>

#include <utility>

namespace async_coro {

/**
 * @brief Suspends current coroutine and calls the continuation function.
 *
 * This function suspends current coroutine and calls the continuation function
 * with a resume function as an argument. The resume function has the signature
 * `void()`, and its invocation immediately resumes the coroutine.
 *
 * @tparam T The type of the continuation function.
 * @param continuation The continuation function to be called.
 * @return An awaitable representing the continuation of the coroutine.
 *
 * @example
 * \code{.cpp}
 * auto continuation = [](auto&& resume) {
 *     // Perform some asynchronous operation
 *     resume(); // Resume the coroutine
 * };
 * co_await async_coro::await_callback(std::move(continuation));
 * // The coroutine will be resumed after the asynchronous operation completes.
 * \endcode
 */
template <typename T, typename R = void>
auto await_callback(T&& continuation) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>) {
  return internal::await_callback<std::decay_t<T>, R>{std::forward<T>(continuation)};
}

/**
 * @brief Suspends current coroutine and calls the continuation function.
 *
 * This function suspends current coroutine and calls the continuation function
 * with a resume function as an argument. The resume function has the signature
 * `void(R)`, and its invocation immediately resumes the coroutine.
 *
 * @tparam T The type of the continuation function.
 * @tparam R The type of the argument to be passed in coroutine on continue.
 * @param continuation The continuation function to be called.
 * @return An awaitable representing the continuation of the coroutine.
 *
 * @example
 * \code{.cpp}
 * auto continuation = [](auto&& resume) {
 *     // Perform some asynchronous operation
 *     resume(calculate_result()); // Resume the coroutine with result
 * };
 * auto res = co_await async_coro::await_callback_with_result<R>(std::move(continuation));
 * // The continuation must call resume(value) where the value is convertible to R.
 * // The coroutine will be resumed after the asynchronous operation completes.
 * \endcode
 */
template <typename R, typename T>
auto await_callback_with_result(T&& continuation) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>) {
  return internal::await_callback<std::decay_t<T>, R>{std::forward<T>(continuation)};
}

}  // namespace async_coro
