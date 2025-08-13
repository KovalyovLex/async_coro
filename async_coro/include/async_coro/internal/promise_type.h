#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_result_holder.h>

#include <utility>

namespace async_coro {

template <typename R>
class task_handle;

template <typename R>
struct task;

}  // namespace async_coro

namespace async_coro::internal {

template <typename T>
concept await_transformable =
    requires(T a) { static_cast<T&&>(a).coro_await_transform(std::declval<async_coro::base_handle&>()); };

template <typename R>
struct promise_type final : internal::promise_result_holder<R> {
  // constructs promise from this
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  // all promises await to be started in scheduler or after embedding
  constexpr auto initial_suspend() noexcept {
    this->init_promise(get_return_object());
    return std::suspend_always();
  }

  // we dont want to destroy our result here
  std::suspend_always final_suspend() noexcept {
    this->on_final_suspend();
    return {};
  }

  template <typename T>
  constexpr decltype(auto) await_transform(T&& in) noexcept {
    // return non standard awaiters as is
    return std::move(in);
  }

  template <await_transformable T>
  constexpr decltype(auto) await_transform(T&& in) {
    return std::forward<T>(in).coro_await_transform(*this);
  }

  void set_owning_by_task_handle(bool owning, internal::passkey_any<task_handle<R>>) {
    base_handle::set_owning_by_task_handle(owning);
  }

  void try_free_task(internal::passkey_any<task<R>>) {
    this->on_task_freed_by_scheduler();
  }

  void set_continuation_functor(callback_base* f, internal::passkey_any<task_handle<R>>) noexcept {
    base_handle::set_continuation_functor(f);
  }

  callback_base* get_continuation_functor(internal::passkey_any<task_handle<R>>) noexcept {
    return base_handle::release_continuation_functor();
  }
};

}  // namespace async_coro::internal
