#pragma once

#include <async_coro/internal/await_when_all.h>

namespace async_coro {

/**
 * @brief Waits for all the given tasks to complete.
 *
 * This function suspends the current coroutine and waits for all the specified tasks to complete before proceeding.
 * The function accepts task_launcher objects which can be constructed from task functions, task objects, or callables
 * that return tasks. Each task can optionally specify an execution queue.
 *
 * @tparam TArgs The return types of the tasks being waited for.
 * @param coroutines The task_launcher objects representing the tasks to wait for.
 * @return An awaitable that resolves to a std::tuple containing the results of all tasks.
 *
 * @note When any of the tasks return void, the corresponding tuple element will be std::monostate.
 *       The function handles mixed return types including void tasks.
 *
 * @example
 * \code{.cpp}
 * // Basic usage with task functions
 * auto results = co_await async_coro::when_all(
 *       async_coro::task_launcher{task1},
 *       async_coro::task_launcher{task2, async_coro::execution_queues::worker}
 * );
 *
 * // Usage with existing task objects
 * auto task1_obj = task1();
 * auto task2_obj = task2();
 * auto results = co_await async_coro::when_all(
 *       async_coro::task_launcher{std::move(task1_obj)},
 *       async_coro::task_launcher{std::move(task2_obj), async_coro::execution_queues::worker}
 * );
 *
 * // This coroutine will resume execution after all tasks have completed.
 * // Process the results using std::apply
 * const auto sum = std::apply(
 *      [&](auto... num) {
 *        return (int(num) + ...);
 *      },
 *      results);
 * \endcode
 */
template <typename... TArgs>
auto when_all(task_launcher<TArgs>... coroutines) {
  return internal::await_when_all(std::move(coroutines)...);
}

}  // namespace async_coro
