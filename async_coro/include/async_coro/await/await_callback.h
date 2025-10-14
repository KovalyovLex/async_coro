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
template <typename T>
auto await_callback(T continuation) noexcept(std::is_nothrow_constructible_v<T, T&&>) {
  return internal::await_callback<T>{std::move(continuation)};
}

}  // namespace async_coro
