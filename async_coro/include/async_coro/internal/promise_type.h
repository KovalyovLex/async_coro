#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
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
    if (auto* continue_with = static_cast<callback_noexcept<void, promise_result<R>&>*>(this->get_continuation_functor())) {
      continue_with->execute(*this);
    }
    return {};
  }

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

  void set_owning_by_task_handle(bool owning, internal::passkey_any<task_handle<R>>) {
    base_handle::set_owning_by_task_handle(owning);
  }

  void try_free_task(internal::passkey_any<task<R>>) {
    this->on_task_freed_by_scheduler();
  }

  void set_continuation_functor(callback_base* f, internal::passkey_any<task_handle<R>>) noexcept {
    base_handle::set_continuation_functor(f);
  }
};

}  // namespace async_coro::internal
