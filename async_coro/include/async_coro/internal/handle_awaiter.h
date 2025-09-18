#pragma once

#include <async_coro/internal/type_traits.h>

#include <cstdint>
#include <utility>

namespace async_coro {
template <class R>
class task_handle;

template <class T>
struct promise_result;
}  // namespace async_coro

namespace async_coro::internal {

template <class... TAwaiters>
struct all_awaiter;

template <class... TAwaiters>
struct any_awaiter;

// wrapper for single task to await with operators || and &&
template <class TRes>
struct handle_awaiter {
  using result_type = TRes;

  explicit handle_awaiter(task_handle<TRes> handle) noexcept : _handle(std::move(handle)) {}

  template <class TRes2>
  auto operator&&(task_handle<TRes2>&& other) && noexcept {
    return all_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  template <class TRes2>
  auto operator||(task_handle<TRes2>&& other) && noexcept {
    return any_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  bool await_ready() const noexcept { return _handle.done(); }

  void cancel_await() noexcept {
    _handle.request_cancel();
  }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, bool>)
  void continue_after_complete(Fx&& continue_f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, bool>) {
    _handle.continue_with([continue_f = std::forward<Fx>(continue_f)](promise_result<TRes>&, bool cancelled) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, bool>) {
      continue_f(cancelled);
    });
  }

  TRes await_resume() {
    return std::move(_handle).get();
  }

 private:
  task_handle<TRes> _handle;
};

}  // namespace async_coro::internal