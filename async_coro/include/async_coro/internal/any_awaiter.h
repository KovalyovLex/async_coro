#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/await_suspension_wrapper.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/tuple_variant_helpers.h>
#include <async_coro/unique_function.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <tuple>
#include <utility>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

// awaiter for coroutines scheduled with || op
template <class... TAwaiters>
struct any_awaiter {
  static_assert(sizeof...(TAwaiters) > 1);

  template <class...>
  friend struct any_awaiter;

  using continue_function_t = async_coro::unique_function<void(bool), sizeof(void*) * 4>;

  using result_type = decltype(get_variant_for_types(replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit any_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  any_awaiter(any_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(!other._continue_f);
  }

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator||(TAwaiter&& other) && noexcept {
    return any_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::move(other)})};
  }

  template <class TRes>
  auto operator||(task_handle<TRes>&& other) && noexcept {
    return any_awaiter<TAwaiters..., handle_awaiter<TRes>>{std::tuple_cat(std::move(_awaiters), std::tuple<handle_awaiter<TRes>>{std::move(other)})};
  }

  template <class... TAwaiters2>
  auto operator||(any_awaiter<TAwaiters2...>&& other) && noexcept {
    return any_awaiter<TAwaiters..., TAwaiters2...>{std::tuple_cat(std::move(_awaiters), std::move(other._awaiters))};
  }

  // (|| ...) && (...) - case
  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator&&(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return all_awaiter{std::tuple<any_awaiter, TAwaiter>{std::move(*this), std::move(other)}};
  }

  // || ... && task - case. Operator && has less precedence so this can happen only in (|| ...) && task case
  template <class TRes>
  auto operator&&(task_handle<TRes>&& other) && noexcept {
    return all_awaiter{std::tuple<any_awaiter, handle_awaiter<TRes>>{std::move(*this), handle_awaiter<TRes>{std::move(other)}}};
  }

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto prepend_awaiter(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return internal::any_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::move(other)), std::move(_awaiters))};
  }

  bool await_ready() noexcept {
    const auto func = [this](size_t awaiter_index) noexcept {
      size_t expected_index = 0;
      if (_result_index.compare_exchange_strong(expected_index, awaiter_index, std::memory_order::relaxed)) {
        // cancel other coroutines
        call_functor_while_true(
            [&](auto& awaiter_to_cancel, auto, size_t index) {
              if (index != awaiter_index) {
                awaiter_to_cancel.cancel_await();
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

  void cancel_await() noexcept {
    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_await(), ...);
        },
        _awaiters);
  }

  void continue_after_complete(continue_function_t continue_f) {
    _continue_f = std::move(continue_f);

    const auto func = [this](auto& awaiter, auto, size_t awaiter_index) {
      awaiter.continue_after_complete([this, awaiter_index](bool cancelled) {
        size_t expected_index = 0;
        if (_result_index.compare_exchange_strong(expected_index, awaiter_index, std::memory_order::relaxed)) {
          // cancel other coroutines
          call_functor_while_true(
              [&](auto& awaiter_to_cancel, auto, size_t index) {
                if (index != awaiter_index) {
                  awaiter_to_cancel.cancel_await();
                }
                return true;
              },
              std::index_sequence_for<TAwaiters...>{});

          _continue_f(cancelled);
        }
      });
      return _result_index.load(std::memory_order::relaxed) == 0;
    };

    call_functor_while_true(func, std::index_sequence_for<TAwaiters...>{});
  }

  result_type await_resume() {
    union_res res;
    const auto index = _result_index.load(std::memory_order::relaxed);

    ASYNC_CORO_ASSERT(index > 0);

    const auto func = [&](auto& awaiter, auto variant_index, size_t awaiter_index) {
      using result_t = decltype(awaiter.await_resume());

      if (index == awaiter_index) {
        if constexpr (std::is_void_v<result_t>) {
          awaiter.await_resume();
          new (&res.r) result_type{variant_index, std::monostate{}};
        } else {
          new (&res.r) result_type{variant_index, awaiter.await_resume()};
        }
        return false;
      }
      return true;
    };

    call_functor_while_true(func, std::index_sequence_for<TAwaiters...>{});

    ASYNC_CORO_WARNINGS_GCC_PUSH
    ASYNC_CORO_WARNINGS_GCC_IGNORE(maybe-uninitialized)

    return std::move(res.r);

    ASYNC_CORO_WARNINGS_GCC_POP
  }

  await_suspension_wrapper<any_awaiter> coro_await_transform(base_handle&) && {
    return {std::move(*this)};
  }

 private:
  template <class F, size_t... TI>
  auto calculate_is_ready(const F& store_result, std::integer_sequence<size_t, TI...>) noexcept {
    return ((std::get<TI>(_awaiters).await_ready() && store_result(TI + 1)) || ...);
  }

  template <class F, size_t... TI>
  void call_functor_while_true(const F& func, std::integer_sequence<size_t, TI...>) {
    ((func(std::get<TI>(_awaiters), std::in_place_index_t<TI>{}, TI + 1)) && ...);
  }

  union union_res {
    union_res() noexcept {}
    ~union_res() noexcept(std::is_nothrow_destructible_v<result_type>) { r.~result_type(); }

    result_type r;
  };

 private:
  std::tuple<TAwaiters...> _awaiters;
  std::atomic_size_t _result_index{0};  // 1 based index
  continue_function_t _continue_f;
};

template <typename... TArgs>
any_awaiter(std::tuple<TArgs>...) -> any_awaiter<TArgs...>;

}  // namespace async_coro::internal