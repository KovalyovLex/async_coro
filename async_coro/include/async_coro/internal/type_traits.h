#pragma once

#include <concepts>
#include <type_traits>

namespace async_coro {
template <typename R>
class task;

namespace internal {
template <class... TAwaiters>
class all_awaiter;

template <class... TAwaiters>
class any_awaiter;
}  // namespace internal

}  // namespace async_coro

namespace async_coro::internal {

template <class T, class R, class... TArgs>
concept is_noexcept_runnable = requires(T func) {
  { func(std::declval<TArgs>()...) } noexcept -> std::same_as<R>;
};

template <class T, class R, class... TArgs>
concept is_runnable = requires(T func) {
  { func(std::declval<TArgs>()...) } -> std::same_as<R>;
};

template <typename T>
struct is_task : std::false_type {};

template <typename R>
struct is_task<task<R>> : std::true_type {};

template <typename T>
inline constexpr bool is_task_v = is_task<T>::value;

template <typename T>
struct unwrap_task {};

template <typename R>
struct unwrap_task<task<R>> {
  using type = R;
};

template <typename T>
using unwrap_task_t = typename unwrap_task<T>::type;

template <typename T>
struct is_any_awaiter : std::false_type {};

template <typename... T>
struct is_any_awaiter<any_awaiter<T...>> : std::true_type {};

template <typename T>
inline constexpr bool is_any_awaiter_v = is_any_awaiter<T>::value;

template <typename T>
struct is_all_awaiter : std::false_type {};

template <typename... T>
struct is_all_awaiter<all_awaiter<T...>> : std::true_type {};

template <typename T>
inline constexpr bool is_all_awaiter_v = is_all_awaiter<T>::value;

}  // namespace async_coro::internal
