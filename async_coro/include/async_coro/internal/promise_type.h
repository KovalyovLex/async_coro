#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/promise_result_holder.h>
#include <async_coro/utils/passkey.h>

#include <utility>

namespace async_coro {

template <typename R>
class task_handle;

template <typename R>
class task;

}  // namespace async_coro

namespace async_coro::internal {

template <typename T>
concept await_transformable = requires(T awaiter) {
  std::forward<T>(awaiter).coro_await_transform(std::declval<async_coro::base_handle&>());
};

template <typename R>
class promise_type final : public internal::promise_result_holder<R> {
 public:
  // constructs promise from this
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  // all promises await to be started in scheduler or after embedding
  constexpr auto initial_suspend() noexcept {
    return std::suspend_always();
  }

  // we dont want to destroy our result here
  std::suspend_always final_suspend() noexcept {
    this->on_final_suspend();
    return {};
  }

  template <typename T>
  constexpr decltype(auto) await_transform(T&& awaiter) noexcept {
    // return non standard awaiters as is
    return std::forward<T>(awaiter);
  }

  template <await_transformable T>
  constexpr decltype(auto) await_transform(T&& awaiter) {
    return std::forward<T>(awaiter).coro_await_transform(*this);
  }

  void set_owning_by_task_handle(bool owning, passkey_any<task_handle<R>> /*key*/) {
    base_handle::set_owning_by_task_handle(owning);
  }

  void set_owning_by_task(bool owning, passkey_any<task<R>> /*key*/) {
    base_handle::set_owning_by_task(owning);
  }

  void set_continuation_functor(callback_base_ptr<false> func, passkey_any<task_handle<R>> /*key*/) noexcept {
    base_handle::set_continuation_functor(std::move(func));
  }

  template <typename TSig>
  auto get_continuation_functor(passkey_any<task_handle<R>> /*key*/) noexcept {
    return base_handle::release_continuation_functor<TSig>();
  }

 protected:
  [[nodiscard]] std::coroutine_handle<> get_handle() noexcept override {
    return get_return_object();
  }
};

}  // namespace async_coro::internal
