#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace async_coro {
class base_handle;
}  // namespace async_coro

namespace async_coro::internal {

class continue_callback;

template <typename T>
concept advanced_awaitable = requires(T awaiter) {
  std::is_move_constructible_v<T>;
  { awaiter.adv_await_ready() } -> std::same_as<bool>;
  { awaiter.cancel_adv_await() };
  { awaiter.adv_await_suspend(std::declval<continue_callback&>()) };
  { awaiter.adv_await_resume() };
};

template <typename T>
concept can_be_awaited = requires(T val) {
  { adv_await_transform(std::forward<T>(val)) } -> advanced_awaitable;
  requires noexcept(adv_await_transform(std::forward<T>(val)));
};

/// CRTP for declaring common operations (like operator&&, operator||, co_await) for advanced awaiters
template <class T>
class advanced_awaiter {
 public:
  advanced_awaiter() = default;

  template <internal::advanced_awaitable TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator&&(TAwaiter&& right) && noexcept;

  template <internal::can_be_awaited TAwaiter>
  auto operator&&(TAwaiter&& right) && noexcept {
    return std::move(*this) && adv_await_transform(std::forward<TAwaiter>(right));
  }

  template <internal::advanced_awaitable TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator||(TAwaiter&& right) && noexcept;

  template <internal::can_be_awaited TAwaiter>
  auto operator||(TAwaiter&& right) && noexcept {
    return std::move(*this) && adv_await_transform(std::forward<TAwaiter>(right));
  }

  auto coro_await_transform(base_handle& handle) && noexcept(std::is_nothrow_constructible_v<T, T&&>);
};

}  // namespace async_coro::internal

namespace async_coro {

// operator&& defines

template <internal::advanced_awaitable TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter1 &&> && std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator&&(TAwaiter1&& left, TAwaiter2&& right) noexcept;

template <internal::can_be_awaited TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator&&(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return adv_await_transform(std::forward<TAwaiter1>(left)) && std::forward<TAwaiter2>(right);
}

template <internal::can_be_awaited TAwaiter1, internal::can_be_awaited TAwaiter2>
auto operator&&(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return adv_await_transform(std::forward<TAwaiter1>(left)) && adv_await_transform(std::forward<TAwaiter2>(right));
}

template <internal::advanced_awaitable TAwaiter1, internal::can_be_awaited TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator&&(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return std::forward<TAwaiter1>(left) && adv_await_transform(std::forward<TAwaiter2>(right));
}

// operator|| defines

template <internal::advanced_awaitable TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter1 &&> && std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator||(TAwaiter1&& left, TAwaiter2&& right) noexcept;

template <internal::can_be_awaited TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator||(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return adv_await_transform(std::forward<TAwaiter1>(left)) || std::forward<TAwaiter2>(right);
}

template <internal::can_be_awaited TAwaiter1, internal::can_be_awaited TAwaiter2>
auto operator||(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return adv_await_transform(std::forward<TAwaiter1>(left)) || adv_await_transform(std::forward<TAwaiter2>(right));
}

template <internal::advanced_awaitable TAwaiter1, internal::can_be_awaited TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter2 &&>)
auto operator||(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  return std::forward<TAwaiter1>(left) || adv_await_transform(std::forward<TAwaiter2>(right));
}

}  // namespace async_coro
