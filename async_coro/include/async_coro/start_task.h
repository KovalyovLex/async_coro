#pragma once

#include <async_coro/internal/await_start_task.h>

namespace async_coro {

/**
 * @brief Schedules parallel task.
 *
 * This function schedules parallel task in scheduler associated with this coroutine and returns task handle.
 *
 * @tparam R The return type of the task.
 * @param task The task to be started.
 * @param execution_queue (Optional) The queue on which the task should be executed.
 *               Defaults to execution_queues::main.
 * @return An awaitable of task_handle<R>
 *
 * @example
 * \code{.cpp}
 * auto handle = co_await async_coro::start_task(my_task);
 * // Use 'handle' to obtain the result of the task when it completes.
 * \endcode
 */
template <typename R>
auto start_task(task<R> task, execution_queue_mark execution_queue = execution_queues::main) {
  return internal::await_start_task(std::move(task), execution_queue);
}

}  // namespace async_coro
