#pragma once

#include <concepts>
#include <utility>

namespace async_coro {
class base_handle_ptr;
}

namespace async_coro::internal {

class continue_callback;

template <typename T>
concept advanced_awaiter = requires(T awaiter) {
  std::is_move_constructible_v<T>;
  { awaiter.await_ready() } -> std::same_as<bool>;
  { awaiter.cancel_await() };
  { awaiter.continue_after_complete(std::declval<continue_callback&>(), std::declval<const base_handle_ptr&>()) };
  { awaiter.await_resume() };
};

}  // namespace async_coro::internal
