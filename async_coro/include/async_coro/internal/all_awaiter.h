#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/internal/tuple_variant_helpers.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/utils/callback_on_stack.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <std::size_t I, class TAwaiter>
class all_awaiter_continue_callback : public callback_on_stack<all_awaiter_continue_callback<I, TAwaiter>, continue_callback> {
 public:
  explicit all_awaiter_continue_callback() noexcept = default;

  void on_destroy() {
    auto& tuple = get_owner_tuple<typename TAwaiter::TCallbacks, I>(*this);
    auto& awaiter = get_owner(tuple, &TAwaiter::_callbacks);

    awaiter.on_continuation_freed();
  }

  continue_callback::return_type on_execute_and_destroy(bool cancelled) {
    auto& tuple = get_owner_tuple<typename TAwaiter::TCallbacks, I>(*this);
    auto& awaiter = get_owner(tuple, &TAwaiter::_callbacks);

    return awaiter.on_continue(cancelled, I);
  }
};

// awaiter for coroutines scheduled with && op
template <class... TAwaiters>
class all_awaiter : public advanced_awaiter<all_awaiter<TAwaiters...>> {
  static_assert((advanced_awaitable<TAwaiters> && ...), "Allowed to use only with advanced awaitable classes");

  template <class...>
  friend class all_awaiter;

  template <size_t I, class>
  friend class all_awaiter_continue_callback;

 public:
  using result_type = remove_void_tuple_t<typename TAwaiters::result_type...>;

  explicit all_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  all_awaiter(const all_awaiter&) = delete;
  all_awaiter(all_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
    ASYNC_CORO_ASSERT(_num_await_free.load(std::memory_order::relaxed) == 0);
  }

  ~all_awaiter() noexcept = default;

  all_awaiter& operator=(const all_awaiter&) = delete;
  all_awaiter& operator=(all_awaiter&&) = delete;

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto append_awaiter(TAwaiter&& other) && noexcept;

  bool adv_await_ready() noexcept {
    return std::apply(
        [&](auto&... awaiters) {
          return (awaiters.adv_await_ready() && ...);
        },
        _awaiters);
  }

  void cancel_adv_await() {
    // barrier to prevent reordering with inner cancel_adv_await call
    _was_any_cancelled.store(true, std::memory_order::release);

    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_adv_await(), ...);
        },
        _awaiters);
  }

  void adv_await_suspend(continue_callback_ptr continue_f, async_coro::base_handle& handle) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _continue_f = std::move(continue_f);
    _num_await_free.store(std::uint32_t(sizeof...(TAwaiters)), std::memory_order::release);

    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      const auto set_continue = [&](auto& awaiter, auto& callback) {
        awaiter.adv_await_suspend(callback.get_ptr(), handle);
      };

      (set_continue(std::get<TI>(_awaiters), std::get<TI>(_callbacks)), ...);
    };

    iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  }

  result_type adv_await_resume() {
    using voids_index_seq = decltype(get_tuple_index_seq_of_voids<std::tuple<typename TAwaiters::result_type...>>());

    if constexpr (!std::is_same_v<voids_index_seq, std::integer_sequence<std::size_t>>) {
      [&]<std::size_t... Ints>(std::integer_sequence<std::size_t, Ints...>) {
        (std::get<Ints>(std::move(_awaiters)).adv_await_resume(), ...);
      }(voids_index_seq{});
    }

    if constexpr (std::is_same_v<result_type, std::tuple<>>) {
      return {};
    } else {
      using result_index_seq = decltype(get_tuple_index_seq_without_voids<std::tuple<typename TAwaiters::result_type...>>());

      return [&]<std::size_t... Ints>(std::integer_sequence<std::size_t, Ints...>) -> result_type {
        return {std::get<Ints>(std::move(_awaiters)).adv_await_resume()...};
      }(result_index_seq{});
    }
  }

  continue_callback::return_type on_continue(bool cancelled, std::size_t awaiter_index) {
    if (cancelled && !_was_any_cancelled.exchange(true, std::memory_order::relaxed)) {
      // immediately notify others to cancel
      const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
        const auto cancel_awaiter = [&](auto& awaiter, std::size_t index) {
          if (index != awaiter_index) {
            awaiter.cancel_adv_await();
          }
        };

        (cancel_awaiter(std::get<TI>(_awaiters), TI), ...);
      };

      iter_awaiters(std::index_sequence_for<TAwaiters...>{});
    }

    on_continuation_freed();
    return {continue_callback_holder{nullptr}, cancelled};
  }

  void on_continuation_freed() noexcept {
    if (_num_await_free.fetch_sub(1, std::memory_order::relaxed) == 1) {
      (void)_num_await_free.load(std::memory_order::acquire);  // to sync _continue_f

      continue_callback_holder continuation{std::move(_continue_f)};
      ASYNC_CORO_ASSERT(continuation);

      auto cancelled = _was_any_cancelled.load(std::memory_order::relaxed);
      while (continuation) {
        std::tie(continuation, cancelled) = continuation.execute_and_destroy(cancelled);
      }
    }
  }

 private:
  static auto create_callbacks() noexcept {
    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      return std::make_tuple(all_awaiter_continue_callback<TI, all_awaiter>{}...);
    };

    return iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  };

 private:
  using TCallbacks = decltype(create_callbacks());

  std::tuple<TAwaiters...> _awaiters;
  TCallbacks _callbacks;
  continue_callback_ptr _continue_f = nullptr;
  std::atomic_uint32_t _num_await_free{0};
  std::atomic_bool _was_any_cancelled{false};
};

template <typename... TArgs>
all_awaiter(std::tuple<TArgs...>&&) -> all_awaiter<TArgs...>;

template <class... TAwaiters>
template <class TAwaiter>
  requires(std::is_rvalue_reference_v<TAwaiter &&>)
inline auto all_awaiter<TAwaiters...>::append_awaiter(TAwaiter&& other) && noexcept {
  if constexpr (is_all_awaiter_v<TAwaiter>) {
    return async_coro::internal::all_awaiter{std::tuple_cat(std::move(_awaiters), std::move(other._awaiters))};
  } else {
    return all_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::forward<TAwaiter>(other)})};
  }
}

}  // namespace async_coro::internal

#include <async_coro/internal/advanced_awaiter.h>
