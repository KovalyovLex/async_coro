#pragma once

#include <async_coro/internal/await_when_all.h>

namespace async_coro {

/**
 * @brief Waits for all the given tasks to complete.
 *
 * This function suspends current coroutine and waits for all the specified tasks to complete before proceeding.
 *
 * @tparam TArgs The argument types of the tasks.
 * @param coroutines The task handles representing the tasks to wait for.
 * @return An awaitable of std::tuple<TArgs>
 *
 * @example
 * \code{.cpp}
 * auto results = co_await async_coro::when_all(
 *       co_await async_coro::start_task(task1()),
 *       co_await async_coro::start_task(task2(), async_coro::execution_queues::worker)
 * );
 *
 * // This coroutine will resume execution after all tasks have completed.
 *
 * const auto sum = std::apply(
 *      [&](auto... num) {
 *        return (int(num) + ...);
 *      },
 *      results);
 * \endcode
 */
template <typename... TArgs>
auto when_all(task_handle<TArgs>... coroutines) {
  return internal::await_when_all(std::move(coroutines)...);
}

}  // namespace async_coro
