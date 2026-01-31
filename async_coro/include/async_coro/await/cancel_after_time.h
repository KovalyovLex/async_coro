#pragma once

#include <async_coro/internal/cancel_after_time.h>

namespace async_coro {

/**
 * @brief Requests cancel of current task after some time
 *
 * @return An awaitable of void
 *
 * @example
 * \code{.cpp}
 * co_await (co_await async_coro::start_task(a_task) || async_coro::cancel_after_time(1s));
 * // code after will be executed if a_task finishes in 1 sec
 * \endcode
 *
 * Coroutine cancel will by default executed the same execution queue.
 * If that queue has only one thread that blocked my long running task, use cancel_after_time with execution_queue_mark parameter
 */
inline auto cancel_after_time(std::chrono::steady_clock::duration sleep_duration) noexcept {
  return internal::cancel_after_time{sleep_duration};
}

/**
 * @brief Requests cancel of current task after some time. Cancellation is happening on provided execution_q
 *
 * @return An awaitable of void
 *
 * @example
 * \code{.cpp}
 * co_await (co_await async_coro::start_task(a_task) || async_coro::cancel_after_time(1s, execution_queues::worker));
 * // code after will be executed if a_task finishes in 1 sec
 * \endcode
 */
inline auto cancel_after_time(std::chrono::steady_clock::duration sleep_duration, execution_queue_mark execution_q) noexcept {
  return internal::cancel_after_time{sleep_duration, execution_q};
}

}  // namespace async_coro
