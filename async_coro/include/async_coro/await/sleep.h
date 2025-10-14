#pragma once

#include <async_coro/internal/await_sleep.h>

namespace async_coro {

/**
 * @brief Awaitable helper: sleep for a relative duration
 *
 * Returns an awaitable that suspends the current coroutine for approximately
 * the given `sleep_duration`. The coroutine will be resumed by the execution
 * system when the steady_clock time point is reached.
 *
 * Usage:
 * @code
 * co_await async_coro::sleep(std::chrono::milliseconds{100});
 * @endcode
 *
 * The resumed coroutine will by default continue on the same execution queue
 * as the awaiting coroutine's parent. To specify a particular queue use the
 * overload that accepts an `execution_queue_mark`.
 */
inline auto sleep(std::chrono::steady_clock::duration sleep_duration) noexcept {
  return internal::await_sleep{sleep_duration};
}

/**
 * @brief Awaitable helper: sleep for a relative duration and resume on a specific queue
 *
 * Same as the single-argument overload but ensures the coroutine is resumed on
 * the provided `execution_q` queue when the timeout expires.
 *
 * Example:
 * @code
 * co_await async_coro::sleep(std::chrono::milliseconds{50}, execution_queues::worker);
 * @endcode
 */
inline auto sleep(std::chrono::steady_clock::duration sleep_duration, execution_queue_mark execution_q) noexcept {
  return internal::await_sleep{sleep_duration, execution_q};
}

}  // namespace async_coro
