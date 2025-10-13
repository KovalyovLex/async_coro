#pragma once

#include <async_coro/internal/get_scheduler_awaiter.h>

namespace async_coro {

/**
 * @brief Returns awaiter to get scheduler.
 *
 * @return An awaitable of async_coro::scheduler&
 *
 * @example
 * \code{.cpp}
 * auto& scheduler = co_await async_coro::get_scheduler();
 * \endcode
 */

// Returns awaiter to get scheduler. Should be used inside task
inline auto get_scheduler() noexcept {
  return internal::get_scheduler_awaiter{};
}

}  // namespace async_coro
