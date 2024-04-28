#pragma once

#include <async_coro/internal/await_when_any.h>

namespace async_coro {

/**
 * @brief Waits for all the given tasks to complete.
 *
 * This function suspends corrent coroutine and waits for all the specified tasks to complete before proceeding.
 *
 * @tparam TArgs The argument types of the tasks.
 * @param coroutines The task handles representing the tasks to wait for.
 * @return An awaitable of std::variant<TArgs>
 *
 * @example
 * \code{.cpp}
 * auto result = co_await async_coro::when_any(
 *       co_await async_coro::start_task(task1()),
 *       co_await async_coro::start_task(task2(), async_coro::execution_thread::worker)
 * );
 *
 * // This coroutine will resume execution after any one of the tasks has completed.
 *
 * int sum = 0;
 * std::visit([&](auto num) { return sum = int(num); }, result);
 * \endcode
 */
template <typename... TArgs>
auto when_any(task_handle<TArgs>... coroutines) {
  return internal::await_when_any(std::move(coroutines)...);
}

}  // namespace async_coro
