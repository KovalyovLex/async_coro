#pragma once

#include <async_coro/internal/await_switch.h>

namespace async_coro {

/**
 * @brief Switches execution to the specified execution queue.
 *
 * This function switches the execution to the specified queue.
 *
 * @param execution_queue The execution queue marker to which the execution should switch.
 * @return An awaitable of void
 *
 * @example
 * \code{.cpp}
 * co_await async_coro::switch_to_queue(async_coro::execution_queues::main);
 * // code after will be executed in the main thread
 * \endcode
 */
inline auto switch_to_queue(execution_queue_mark execution_queue) {
  return internal::await_switch{execution_queue};
}

}  // namespace async_coro
