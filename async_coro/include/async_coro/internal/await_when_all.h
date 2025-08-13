#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/scheduler.h>
#include <async_coro/task_handle.h>
#include <async_coro/task_launcher.h>

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <tuple>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <typename... TArgs>
struct await_when_all {
  using result_type = remove_void_tuple_t<TArgs...>;

  explicit await_when_all(task_launcher<TArgs>... launchers) noexcept
      : _launchers(std::move(launchers)...) {
    static_assert(sizeof...(TArgs) > 0);
  }

  await_when_all(const await_when_all&) = delete;
  await_when_all(await_when_all&&) = delete;

  await_when_all& operator=(await_when_all&&) = delete;
  await_when_all& operator=(const await_when_all&) = delete;

  void embed_task(base_handle& parent) {
    scheduler& scheduler = parent.get_scheduler();

    [&]<size_t... Ints>(std::integer_sequence<size_t, Ints...>) {
      ((std::get<Ints>(_coroutines) = scheduler.start_task(std::move(std::get<Ints>(_launchers)))), ...);
    }(std::make_index_sequence<sizeof...(TArgs)>{});
  }

  bool await_ready() const noexcept {
    ASYNC_CORO_ASSERT(_suspended.load(std::memory_order::relaxed) == false);

    return std::apply(
        [&](const auto&... coros) {
          return (coros.done() && ...);
        },
        _coroutines);
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    h.promise().on_suspended();

    const auto continue_f = [&](auto& coro) {
      coro.continue_with([this, h](auto&) noexcept {
        if (this->_counter.fetch_sub(1, std::memory_order::relaxed) == 1) {
          // continue this coro

          base_handle& handle = h.promise();
          if (_suspended.load(std::memory_order::acquire)) {
            handle.get_scheduler().continue_execution(handle);
          } else {
            handle.get_scheduler().plan_continue_execution(handle);
          }
        }
      });
    };

    std::apply(
        [&](auto&... coros) {
          (continue_f(coros), ...);
        },
        _coroutines);

    _suspended.store(true, std::memory_order::release);
  }

  result_type await_resume() {
    std::apply(
        [&](auto&... coros) {
          (coros.check_exception(), ...);
        },
        _coroutines);

    return [&]<size_t... Ints>(std::integer_sequence<size_t, Ints...>) -> result_type {
      if constexpr (std::is_same_v<result_type, std::tuple<>>) {
        return {};
      } else {
        return {std::get<Ints>(std::move(_coroutines)).get()...};
      }
    }(result_index_seq{});
  }

 private:
  template <size_t I, size_t... Ints1, size_t... Ints2>
  static auto get_filtered_index_sequence(std::integer_sequence<size_t, Ints1...>, std::integer_sequence<size_t, I, Ints2...>) {
    using tuple_t = std::tuple<task_handle<TArgs>...>;
    using tuple_value_t = std::remove_cvref_t<decltype(std::get<I>(std::declval<tuple_t>()))>;

    if constexpr (std::is_same_v<tuple_value_t, task_handle<void>>) {
      return get_filtered_index_sequence(std::integer_sequence<size_t, Ints1...>{}, std::integer_sequence<size_t, Ints2...>{});
    } else {
      return get_filtered_index_sequence(std::integer_sequence<size_t, Ints1..., I>{}, std::integer_sequence<size_t, Ints2...>{});
    }
  }

  template <size_t... Ints>
  static std::integer_sequence<size_t, Ints...> get_filtered_index_sequence(std::integer_sequence<size_t, Ints...>, std::integer_sequence<size_t>) {
    return {};
  }

  using result_index_seq = decltype(get_filtered_index_sequence(std::integer_sequence<size_t>{}, std::index_sequence_for<TArgs...>{}));

 private:
  std::tuple<task_launcher<TArgs>...> _launchers;
  std::tuple<task_handle<TArgs>...> _coroutines;
  std::atomic_size_t _counter{sizeof...(TArgs)};
  std::atomic_bool _suspended{false};
};

template <typename... TArgs>
await_when_all(task_launcher<TArgs>...) -> await_when_all<TArgs...>;

}  // namespace async_coro::internal
