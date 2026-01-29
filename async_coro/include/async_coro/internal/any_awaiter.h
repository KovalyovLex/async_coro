#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/tuple_variant_helpers.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/utils/callback_on_stack.h>
#include <async_coro/utils/get_owner.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <std::size_t I, class TAwaiter>
class any_awaiter_continue_callback : public callback_on_stack<any_awaiter_continue_callback<I, TAwaiter>, continue_callback> {
 public:
  explicit any_awaiter_continue_callback() noexcept = default;

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

// awaiter for coroutines scheduled with || op
template <class... TAwaiters>
class any_awaiter : public advanced_awaiter<any_awaiter<TAwaiters...>> {
  static_assert((advanced_awaitable<TAwaiters> && ...), "Allowed to use only with advanced awaitable classes");
  static_assert(sizeof...(TAwaiters) > 0);

  template <class...>
  friend class any_awaiter;

  template <size_t I, class>
  friend class any_awaiter_continue_callback;

 public:
  using result_type = decltype(get_variant_for_types(replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit any_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept
      : _awaiters(std::move(awaiters)) {}

  any_awaiter(const any_awaiter&) = delete;
  any_awaiter(any_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
  }

  ~any_awaiter() noexcept = default;

  any_awaiter& operator=(const any_awaiter&) = delete;
  any_awaiter& operator=(any_awaiter&&) = delete;

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto append_awaiter(TAwaiter&& other) && noexcept;

  bool adv_await_ready() noexcept {
    const auto func = [this](std::size_t awaiter_index) noexcept {
      std::uint32_t expected_index = 0;
      if (_result_index.compare_exchange_strong(expected_index, std::uint32_t(awaiter_index + 1), std::memory_order::relaxed)) {
        // cancel other coroutines
        call_functor_while_true(
            [&](auto& awaiter_to_cancel, auto, std::size_t index) {
              if (index != awaiter_index) {
                awaiter_to_cancel.cancel_adv_await();
              }
              return true;
            },
            std::index_sequence_for<TAwaiters...>{});

        return true;
      }
      return false;
    };

    return calculate_is_ready(func, std::index_sequence_for<TAwaiters...>{});
  }

  void cancel_adv_await() {
    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_adv_await(), ...);
        },
        _awaiters);
  }

  void adv_await_suspend(continue_callback_ptr continue_f, async_coro::base_handle& handle) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _continue_f = std::move(continue_f);
    _num_await_free.store(1, std::memory_order::release);  // using ac_rel for _continue syncronization

    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      const auto set_continue = [&](auto& awaiter, auto& callback) {
        _num_await_free.fetch_add(1, std::memory_order::relaxed);
        awaiter.adv_await_suspend(callback.get_ptr(), handle);
        return _result_index.load(std::memory_order::relaxed) == 0;
      };

      // iterate while _result_index is zero
      (void)(set_continue(std::get<TI>(_awaiters), std::get<TI>(_callbacks)) && ...);
    };

    iter_awaiters(std::index_sequence_for<TAwaiters...>{});

    // decrease 1 that we set above
    on_continuation_freed();
  }

  continue_callback::return_type on_continue(bool cancelled, std::size_t awaiter_index) {
    std::uint32_t expected_index = 0;
    if (!cancelled && _result_index.compare_exchange_strong(expected_index, std::uint32_t(awaiter_index + 1), std::memory_order::relaxed)) {
      // cancel other coroutines
      call_functor_while_true(
          [&](auto& awaiter_to_cancel, auto, std::size_t index) {
            if (index != awaiter_index) {
              awaiter_to_cancel.cancel_adv_await();
            }
            return true;
          },
          std::index_sequence_for<TAwaiters...>{});

      // we cant call _continue_f here as all callbacks should be finished to guarantee lifetime of coroutine
    }

    on_continuation_freed();
    return {continue_callback_holder{nullptr}, false};
  }

  void on_continuation_freed() noexcept {
    if (_num_await_free.fetch_sub(1, std::memory_order::relaxed) == 1) {
      (void)_num_await_free.load(std::memory_order::acquire);  // to sync _continue_f

      continue_callback_holder continuation{std::move(_continue_f)};
      ASYNC_CORO_ASSERT(continuation);

      auto cancelled = _result_index.load(std::memory_order::relaxed) == 0;
      while (continuation) {
        std::tie(continuation, cancelled) = continuation.execute_and_destroy(cancelled);
      }
    }
  }

  result_type adv_await_resume() {
    union_res res;
    std::size_t index = _result_index.load(std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(index > 0);
    --index;

    const auto func = [&](auto& awaiter, auto variant_index, std::size_t awaiter_index) {
      using result_t = decltype(awaiter.adv_await_resume());

      if (index == awaiter_index) {
        if constexpr (std::is_void_v<result_t>) {
          awaiter.adv_await_resume();
          new (&res.r) result_type{variant_index, std::monostate{}};
        } else {
          new (&res.r) result_type{variant_index, awaiter.adv_await_resume()};
        }
        return false;
      }
      return true;
    };

    call_functor_while_true(func, std::index_sequence_for<TAwaiters...>{});

    ASYNC_CORO_WARNINGS_GCC_PUSH
    ASYNC_CORO_WARNINGS_GCC_IGNORE("maybe-uninitialized")

    return std::move(res.r);

    ASYNC_CORO_WARNINGS_GCC_POP
  }

 private:
  template <class F, std::size_t... TI>
  auto calculate_is_ready(const F& store_result, std::integer_sequence<std::size_t, TI...> /*seq*/) noexcept {
    return ((std::get<TI>(_awaiters).adv_await_ready() && store_result(TI)) || ...);
  }

  template <class F, std::size_t... TI>
  void call_functor_while_true(const F& func, std::integer_sequence<std::size_t, TI...> /*seq*/) {
    ((func(std::get<TI>(_awaiters), std::in_place_index_t<TI>{}, TI)) && ...);
  }

  static auto create_callbacks() noexcept {
    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      return std::make_tuple(any_awaiter_continue_callback<TI, any_awaiter>{}...);
    };

    return iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  };

  union union_res {
    union_res() noexcept {}
    ~union_res() noexcept(std::is_nothrow_destructible_v<result_type>) { r.~result_type(); }
    union_res(const union_res&) = delete;
    union_res(union_res&&) = delete;

    union_res& operator=(const union_res&) = delete;
    union_res& operator=(union_res&&) = delete;

    result_type r;
  };

 private:
  using TCallbacks = decltype(create_callbacks());

  std::tuple<TAwaiters...> _awaiters;
  TCallbacks _callbacks;
  continue_callback_ptr _continue_f = nullptr;
  std::atomic_uint32_t _result_index{0};  // 1 based index
  std::atomic_uint32_t _num_await_free{0};
};

template <typename... TArgs>
any_awaiter(std::tuple<TArgs...>&&) -> any_awaiter<TArgs...>;

template <class... TAwaiters>
template <class TAwaiter>
  requires(std::is_rvalue_reference_v<TAwaiter &&>)
inline auto any_awaiter<TAwaiters...>::append_awaiter(TAwaiter&& other) && noexcept {
  if constexpr (is_any_awaiter_v<TAwaiter>) {
    return async_coro::internal::any_awaiter{std::tuple_cat(std::move(_awaiters), std::move(other._awaiters))};
  } else {
    return any_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::forward<TAwaiter>(other)})};
  }
}

}  // namespace async_coro::internal

#include <async_coro/internal/advanced_awaiter.h>
