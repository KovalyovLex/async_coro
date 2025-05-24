#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <atomic>
#include <coroutine>
#include <tuple>

namespace async_coro {

template <typename R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <typename... TArgs>
struct await_when_all {
  explicit await_when_all(task_handle<TArgs>... coroutines) noexcept
      : _coroutines(std::move(coroutines)...) {
    static_assert(sizeof...(TArgs) > 0);
  }

  await_when_all(const await_when_all&) = delete;
  await_when_all(await_when_all&&) = delete;

  await_when_all& operator=(await_when_all&&) = delete;
  await_when_all& operator=(const await_when_all&) = delete;

  bool await_ready() const noexcept {
    return std::apply(
        [&](const auto&... coros) {
          return (coros.done() && ...);
        },
        _coroutines);
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    const auto continue_f = [&](auto& coro) {
      coro.continue_with([this, h](auto&) noexcept {
        if (this->_counter.fetch_sub(1, std::memory_order::relaxed) == 1) {
          // continue this coro

          base_handle& handle = h.promise();
          handle.get_scheduler().plan_continue_execution(handle);
        }
      });
    };

    std::apply(
        [&](auto&... coros) {
          (continue_f(coros), ...);
        },
        _coroutines);
  }

  std::tuple<TArgs...> await_resume() {
    return std::apply(
        [&](auto&... coros) {
          return std::tuple<TArgs...>{coros...};
        },
        _coroutines);
  }

 private:
  std::atomic_size_t _counter = sizeof...(TArgs);
  std::tuple<task_handle<TArgs>...> _coroutines;
};

template <typename... TArgs>
await_when_all(task_handle<TArgs>...) -> await_when_all<TArgs...>;

}  // namespace async_coro::internal
