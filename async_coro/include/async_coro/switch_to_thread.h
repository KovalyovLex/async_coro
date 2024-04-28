#pragma once

#include <async_coro/internal/await_switch.h>

namespace async_coro {
/**
 * @brief Switches execution to the specified thread.
 *
 * This function switches the execution to the specified thread.
 *
 * @param thread The thread type to which the execution should switch.
 * @return An awaitable of void
 *
 * @example
 * \code{.cpp}
 * co_await async_coro::switch_to_thread(async_coro::execution_thread::main);
 * // code after will be executed in the main thread
 * \endcode
 */
auto switch_to_thread(execution_thread thread) {
  return internal::await_switch{thread};
}
}  // namespace async_coro
