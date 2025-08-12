#pragma once

#include <async_coro/internal/await_start_task.h>
#include <async_coro/task_launcher.h>

namespace async_coro {

/**
 * @brief Schedules parallel task.
 *
 * This function schedules parallel task in scheduler associated with this coroutine and returns task handle.
 *
 * @tparam R The return type of the task.
 * @param task The task to be started.
 * @return An awaitable of task_handle<R>
 *
 * @example
 * \code{.cpp}
 * auto handle = co_await async_coro::start_task(my_task);
 * // Use 'handle' to obtain the result of the task when it completes.
 * \endcode
 */
template <typename R>
auto start_task(task_launcher<R> launcher) {
  return internal::await_start_task(std::move(launcher));
}

}  // namespace async_coro
