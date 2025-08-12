#pragma once

#include <async_coro/internal/await_when_any.h>

namespace async_coro {

/**
 * @brief Waits for any of the given tasks to complete.
 *
 * This function suspends current coroutine and waits for any one of the specified tasks to complete before proceeding.
 * The function returns as soon as the first task completes, with the result of that task.
 *
 * @tparam TArgs The argument types of the tasks.
 * @param coroutines The task handles representing the tasks to wait for.
 * @return An awaitable of std::variant<TArgs> containing the result of the first completed task.
 *
 * @example
 * \code{.cpp}
 * auto result = co_await async_coro::when_any(
 *       async_coro::task_launcher{task1},
 *       async_coro::task_launcher{task2(), async_coro::execution_queues::worker}
 * );
 *
 * // This coroutine will resume execution after any one of the tasks has completed.
 *
 * int sum = 0;
 * std::visit([&](auto num) { sum = int(num); }, result);
 * \endcode
 */
template <typename... TArgs>
auto when_any(task_launcher<TArgs>... coroutines) {
  return internal::await_when_any(std::move(coroutines)...);
}

}  // namespace async_coro
