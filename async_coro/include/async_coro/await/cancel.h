#pragma once

#include <async_coro/internal/await_cancel_task.h>

namespace async_coro {

/**
 * @brief Requests cancel of current task
 *
 * @return An awaitable of void
 *
 * @example
 * \code{.cpp}
 * co_await async_coro::cancel();
 * // code after will newer be executed
 * \endcode
 */
inline auto cancel() {
  return internal::await_cancel_task{};
}

}  // namespace async_coro