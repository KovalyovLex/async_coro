#pragma once

#include <async_coro/internal/execute_after_time.h>

#include <type_traits>

namespace async_coro {

/**
 * @brief Suspends coroutine and executes function Fx after provided sleep
 *
 * @return An awaitable of result of Fx()
 *
 * @example
 * \code{.cpp}
 * auto res = co_await (co_await async_coro::start_task(a_task) || async_coro::execute_after_time([](){ return 23; }, 1s));
 * // result of operation will be result of a_task of 23 if a_task haven't finished in approximate 1 sec
 * \endcode
 */
template <class Fx>
  requires(std::is_invocable_v<Fx>)
inline auto execute_after_time(Fx func, std::chrono::steady_clock::duration sleep_duration) noexcept(std::is_nothrow_move_constructible_v<Fx>) {
  return internal::execute_after_time<Fx>{std::move(func), sleep_duration};
}

}  // namespace async_coro
