#pragma once

#include <async_coro/internal/task_awaiters.h>

#include <tuple>

namespace async_coro {

/**
 * @brief Waits for all the given tasks to complete.
 *
 * This operator returns awaiter to suspend the current coroutine and wait for all the specified tasks to complete before proceeding.
 *
 * @return An awaitable that resolves to a std::tuple containing the results of all tasks.
 *
 * @note When any of the tasks return void, the corresponding tuple element will be std::monostate.
 *       The function handles mixed return types including void tasks.
 *
 * @example
 * \code{.cpp}
 * auto results = co_await (scheduler.start_task(task1) && scheduler.start_task(task2));
 *
 * // This coroutine will resume execution after all of the tasks has completed.
 *
 * \endcode
 */
template <class TRes1, class TRes2>
auto operator&&(task_handle<TRes1>&& a, task_handle<TRes2>&& b) noexcept {
  return internal::all_awaiter{std::make_tuple(internal::handle_awaiter<TRes1>{std::move(a)}, internal::handle_awaiter<TRes2>{std::move(b)})};
}

template <class TRes1, class... TAwaitables>
auto operator&&(task_handle<TRes1>&& a, internal::any_awaiter<TAwaitables...>&& b) noexcept {
  return internal::all_awaiter{std::make_tuple(internal::handle_awaiter<TRes1>{std::move(a)}, std::move(b))};
}

template <class TRes1, class... TAwaitables>
auto operator&&(task_handle<TRes1>&& a, internal::all_awaiter<TAwaitables...>&& b) noexcept {
  return std::move(b).prepend_awaiter(internal::handle_awaiter<TRes1>{std::move(a)});
}

// Delete lvalue overloads to prevent use-after-move
template <class TRes1, class TRes2>
auto operator&&(task_handle<TRes1>& a, task_handle<TRes2>& b) = delete;
template <class TRes1, class TRes2>
auto operator&&(task_handle<TRes1>&& a, task_handle<TRes2>& b) = delete;
template <class TRes1, class TRes2>
auto operator&&(task_handle<TRes1>& a, task_handle<TRes2>&& b) = delete;

/**
 * @brief Waits for any of the given tasks to complete.
 *
 * This function suspends current coroutine and waits for any one of the specified tasks to complete before proceeding.
 * The function returns as soon as the first task completes, with the result of that task.
 *
 * @return An awaitable of std::variant<TArgs> containing the result of the first completed task.
 *
 * @example
 * \code{.cpp}
 * auto results = co_await (scheduler.start_task(task1) || scheduler.start_task(task2));
 *
 * // This coroutine will resume execution after any one of the tasks has completed.
 *
 * \endcode
 */
template <class TRes1, class TRes2>
auto operator||(task_handle<TRes1>&& a, task_handle<TRes2>&& b) noexcept {
  return internal::any_awaiter{std::make_tuple(internal::handle_awaiter<TRes1>{std::move(a)}, internal::handle_awaiter<TRes2>{std::move(b)})};
}

template <class TRes1, class... TAwaitables>
auto operator||(task_handle<TRes1>&& a, internal::any_awaiter<TAwaitables...>&& b) noexcept {
  return std::move(b).prepend_awaiter(internal::handle_awaiter<TRes1>{std::move(a)});
}

template <class TRes1, class... TAwaitables>
auto operator||(task_handle<TRes1>&& a, internal::all_awaiter<TAwaitables...>&& b) noexcept {
  return internal::any_awaiter{std::make_tuple(internal::handle_awaiter<TRes1>{std::move(a)}, std::move(b))};
}

// Delete lvalue overloads to prevent use-after-move
template <class TRes1, class TRes2>
auto operator||(task_handle<TRes1>& a, task_handle<TRes2>& b) = delete;
template <class TRes1, class TRes2>
auto operator||(task_handle<TRes1>&& a, task_handle<TRes2>& b) = delete;
template <class TRes1, class TRes2>
auto operator||(task_handle<TRes1>& a, task_handle<TRes2>&& b) = delete;

}  // namespace async_coro