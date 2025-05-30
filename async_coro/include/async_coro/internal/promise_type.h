#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_result_holder.h>

namespace async_coro {

template <typename R>
class task_handle;

template <typename R>
struct task;

}  // namespace async_coro

namespace async_coro::internal {

template <typename T>
concept embeddable_task =
    requires(T a) { a.embed_task(std::declval<async_coro::base_handle&>()); };

template <typename R>
struct promise_type final : internal::promise_result_holder<R> {
  // construct my promise from me
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  // all promises await to be started in scheduler or after embedding
  constexpr auto initial_suspend() noexcept {
    this->init_promise(get_return_object());
    return std::suspend_always();
  }

  // we dont want to destroy our result here
  std::suspend_always final_suspend() noexcept;

  template <typename T>
  constexpr decltype(auto) await_transform(T&& in) noexcept {
    // return non standard awaiters as is
    return std::move(in);
  }

  template <embeddable_task T>
  constexpr decltype(auto) await_transform(T&& in) {
    in.embed_task(*this);
    return std::move(in);
  }

  void set_task_handle(task_handle<R>* handle, internal::passkey_any<task_handle<R>>) {
    this->set_task_handle_impl(handle);
  }

  void try_free_task(internal::passkey_any<task<R>>) {
    this->try_free_task_impl();
  }
};

}  // namespace async_coro::internal
