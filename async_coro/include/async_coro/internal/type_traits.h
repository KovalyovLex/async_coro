#pragma once

#include <concepts>
#include <type_traits>

namespace async_coro {

template <typename R>
class task;

}

namespace async_coro::internal {

template <class T, class R, class... TArgs>
concept is_noexcept_runnable = requires(T a) {
  { a(std::declval<TArgs>()...) } noexcept -> std::same_as<R>;
};

template <class T, class R, class... TArgs>
concept is_runnable = requires(T a) {
  { a(std::declval<TArgs>()...) } -> std::same_as<R>;
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

}  // namespace async_coro::internal
