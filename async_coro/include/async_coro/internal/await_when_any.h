#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>

#include <atomic>
#include <coroutine>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace async_coro {

template <typename R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <class... TArgs>
struct types_container {
};

template <class T, class... TArgs1, class... TArgs2>
static auto get_filtered_variant_types(types_container<TArgs1...>, types_container<T, TArgs2...>) {
  if constexpr (std::is_void_v<T>) {
    return get_filtered_variant_types(types_container<TArgs1..., std::monostate>{}, types_container<TArgs2...>{});
  } else {
    return get_filtered_variant_types(types_container<TArgs1..., T>{}, types_container<TArgs2...>{});
  }
}

template <class... TArgs>
static types_container<TArgs...> get_filtered_variant_types(types_container<TArgs...>, types_container<>) {
  return {};
}

template <class... TArgs>
static auto get_filtered_variant(types_container<TArgs...>) {
  return std::variant<TArgs...>{};
}

template <typename... TArgs>
struct await_when_any {
  using result_type = decltype(get_filtered_variant(get_filtered_variant_types(types_container<>{}, types_container<TArgs...>{})));

  explicit await_when_any(task_handle<TArgs>... coroutines) noexcept
      : _coroutines(std::move(coroutines)...) {
    static_assert(sizeof...(TArgs) > 0);
  }

  await_when_any(const await_when_any&) = delete;
  await_when_any(await_when_any&&) = delete;

  await_when_any& operator=(await_when_any&&) = delete;
  await_when_any& operator=(const await_when_any&) = delete;

  ~await_when_any() {
    if (_has_result.load(std::memory_order_relaxed)) {
      std::destroy_at(&_result);
    }
  }

  bool await_ready() noexcept {
    const auto store_result = [this](auto& coro, auto index) noexcept -> bool {
      bool expected = false;
      if (this->_has_result.compare_exchange_strong(expected, true, std::memory_order::relaxed)) {
        using result_t = decltype(coro.get());
        if constexpr (!std::is_void_v<result_t>) {
          new (&_result) result_type{index, std::move(coro.get())};
        } else {
          new (&_result) result_type{index, std::monostate{}};
        }
      }
      return true;
    };

    return calculate_is_ready(store_result, std::index_sequence_for<TArgs...>{});
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    h.promise().on_suspended();

    const auto continue_f = [&](auto& coro, auto index) noexcept {
      using result_t = decltype(coro.get());

      coro.continue_with([this, h, index](auto& res) noexcept {
        bool expected = false;
        if (this->_has_result.compare_exchange_strong(expected, true, std::memory_order::relaxed)) {
          // continue this coro
          if constexpr (!std::is_void_v<result_t>) {
            new (&_result) result_type{index, res.move_result()};
          } else {
            new (&_result) result_type{index, std::monostate{}};
          }
          base_handle& handle = h.promise();
          handle.get_scheduler().plan_continue_execution(handle);
        }
      });
    };

    call_functor(continue_f, std::index_sequence_for<TArgs...>{});
  }

  result_type await_resume() {
    ASYNC_CORO_ASSERT(_has_result.load(std::memory_order_relaxed));

    return std::move(_result);
  }

 private:
  template <class F, size_t... TI>
  auto calculate_is_ready(const F& store_result, std::integer_sequence<size_t, TI...>) {
    return ((std::get<TI>(_coroutines).done() && store_result(std::get<TI>(_coroutines), std::in_place_index_t<TI>{})) || ...);
  }

  template <class F, size_t... TI>
  void call_functor(const F& func, std::integer_sequence<size_t, TI...>) {
    (func(std::get<TI>(_coroutines), std::in_place_index_t<TI>{}), ...);
  }

 private:
  std::atomic_bool _has_result = false;
  std::tuple<task_handle<TArgs>...> _coroutines;
  union {
    result_type _result;
  };
};

template <typename... TArgs>
await_when_any(task_handle<TArgs>...) -> await_when_any<TArgs...>;

}  // namespace async_coro::internal
