#pragma once

#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/internal/all_awaiter.h>
#include <async_coro/internal/any_awaiter.h>
#include <async_coro/internal/await_suspension_wrapper.h>
#include <async_coro/internal/type_traits.h>

namespace async_coro {

template <internal::advanced_awaitable TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter1 &&> && std::is_rvalue_reference_v<TAwaiter2 &&>)
inline auto operator&&(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  if constexpr (internal::is_all_awaiter_v<TAwaiter1>) {
    return std::forward<TAwaiter1>(left).append_awaiter(std::forward<TAwaiter2>(right));
  } else {
    return internal::all_awaiter<TAwaiter1>{std::forward<TAwaiter1>(left)}.append_awaiter(std::forward<TAwaiter2>(right));
  }
}

template <internal::advanced_awaitable TAwaiter1, internal::advanced_awaitable TAwaiter2>
  requires(std::is_rvalue_reference_v<TAwaiter1 &&> && std::is_rvalue_reference_v<TAwaiter2 &&>)
inline auto operator||(TAwaiter1&& left, TAwaiter2&& right) noexcept {
  if constexpr (internal::is_any_awaiter_v<TAwaiter1>) {
    return std::forward<TAwaiter1>(left).append_awaiter(std::forward<TAwaiter2>(right));
  } else {
    return internal::any_awaiter<TAwaiter1>{std::forward<TAwaiter1>(left)}.append_awaiter(std::forward<TAwaiter2>(right));
  }
}

}  // namespace async_coro

namespace async_coro::internal {

template <class T>
template <internal::advanced_awaitable TAwaiter>
  requires(std::is_rvalue_reference_v<TAwaiter &&>)
inline auto advanced_awaiter<T>::operator&&(TAwaiter&& right) && noexcept {
  if constexpr (is_all_awaiter_v<T>) {
    return std::move(static_cast<T&>(*this)).append_awaiter(std::forward<TAwaiter>(right));
  } else {
    return all_awaiter<T>{std::move(static_cast<T&>(*this))}.append_awaiter(std::forward<TAwaiter>(right));
  }
}

template <class T>
template <internal::advanced_awaitable TAwaiter>
  requires(std::is_rvalue_reference_v<TAwaiter &&>)
inline auto advanced_awaiter<T>::operator||(TAwaiter&& right) && noexcept {
  if constexpr (is_any_awaiter_v<T>) {
    return std::move(static_cast<T&>(*this)).append_awaiter(std::forward<TAwaiter>(right));
  } else {
    return any_awaiter<T>{std::move(static_cast<T&>(*this))}.append_awaiter(std::forward<TAwaiter>(right));
  }
}

template <class T>
inline auto advanced_awaiter<T>::coro_await_transform(base_handle& /*handle*/) && noexcept(std::is_nothrow_constructible_v<T, T&&>) {
  return await_suspension_wrapper<T>{std::move(*static_cast<T*>(this))};
}

}  // namespace async_coro::internal
