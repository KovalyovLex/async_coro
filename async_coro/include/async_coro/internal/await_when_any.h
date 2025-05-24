#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <atomic>
#include <coroutine>
#include <tuple>
#include <variant>

namespace async_coro {

template <typename R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <typename... TArgs>
struct await_when_any {
  explicit await_when_any(task_handle<TArgs>... coroutines) noexcept
      : _coroutines(std::move(coroutines)...) {
    static_assert(sizeof...(TArgs) > 0);
  }

  await_when_any(const await_when_any&) = delete;
  await_when_any(await_when_any&&) = delete;

  await_when_any& operator=(await_when_any&&) = delete;
  await_when_any& operator=(const await_when_any&) = delete;

  ~await_when_any() {
    std::destroy_at(&_result);
  }

  bool await_ready() noexcept {
    const auto store_result = [this](auto& coro) noexcept -> bool {
      bool expected = false;
      if (this->_has_result.compare_exchange_strong(expected, true, std::memory_order::relaxed)) {
        new (&_result) std::variant<TArgs...>(std::move(coro.get()));
      }
      return true;
    };

    return std::apply(
        [&](const auto&... coros) noexcept {
          return ((coros.done() && store_result(coros)) || ...);
        },
        _coroutines);
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    const auto continue_f = [&](auto& coro) noexcept {
      coro.continue_with([this, h](auto& res) noexcept {
        bool expected = false;
        if (this->_has_result.compare_exchange_strong(expected, true, std::memory_order::relaxed)) {
          // continue this coro
          new (&_result) std::variant<TArgs...>(res.move_result());
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

  std::variant<TArgs...> await_resume() {
    return std::move(_result);
  }

 private:
  std::atomic_bool _has_result = false;
  std::tuple<task_handle<TArgs>...> _coroutines;
  union {
    std::variant<TArgs...> _result;
  };
};

template <typename... TArgs>
await_when_any(task_handle<TArgs>...) -> await_when_any<TArgs...>;

}  // namespace async_coro::internal
