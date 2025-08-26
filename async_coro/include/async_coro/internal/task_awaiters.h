#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/always_false.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/scheduler.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace async_coro {
template <typename R>
class task_handle;
}

namespace async_coro::internal {

template <class... TArgs>
struct types_holder {
  template <size_t I>
  constexpr auto get() const noexcept {
  }
};

template <class... TArgs>
constexpr types_holder<TArgs...> replace_void_type_in_holder_impl(types_holder<TArgs...>, types_holder<>) noexcept {
  return {};
}

template <class T, class... TArgs1, class... TArgs2>
constexpr auto replace_void_type_in_holder_impl(types_holder<TArgs1...>, types_holder<T, TArgs2...>) noexcept {
  if constexpr (std::is_void_v<T>) {
    return replace_void_type_in_holder_impl(types_holder<TArgs1..., std::monostate>{}, types_holder<TArgs2...>{});
  } else {
    return replace_void_type_in_holder_impl(types_holder<TArgs1..., T>{}, types_holder<TArgs2...>{});
  }
}

template <class... TArgs>
constexpr auto replace_void_type_in_holder(types_holder<TArgs...>) noexcept {
  return replace_void_type_in_holder_impl(types_holder<>{}, types_holder<TArgs...>{});
}

template <class... TArgs>
constexpr auto get_variant_for_types(types_holder<TArgs...>) noexcept {
  return std::variant<TArgs...>{};
}

template <size_t... Ints, class TTuple, class TElem>
constexpr auto replace_last_tuple_elem_impl(std::integer_sequence<size_t, Ints...>, TTuple&& tuple, TElem&& new_last_elem) noexcept {
  static_assert(!std::is_reference_v<TElem>);

  using tuple_t = std::tuple<typename std::tuple_element<Ints, TTuple>::type..., TElem>;

  return tuple_t{std::get<Ints>(std::move(tuple))..., std::move(new_last_elem)};
}

template <class TTuple, class TElem>
constexpr auto replace_last_tuple_elem(TTuple&& tuple, TElem&& new_last_elem) noexcept {
  static_assert(std::tuple_size_v<TTuple> > 1);

  return replace_last_tuple_elem_impl(std::make_index_sequence<std::tuple_size_v<TTuple> - 1>{}, std::forward<TTuple>(tuple), std::forward<TElem>(new_last_elem));
}

template <class TTuple, size_t... Ints>
constexpr std::integer_sequence<size_t, Ints...> get_tuple_index_seq_without_voids_impl(std::integer_sequence<size_t, Ints...>, std::integer_sequence<size_t>) {
  return {};
}

template <class TTuple, size_t I, size_t... Ints1, size_t... Ints2>
constexpr auto get_tuple_index_seq_without_voids_impl(std::integer_sequence<size_t, Ints1...>, std::integer_sequence<size_t, I, Ints2...>) {
  if constexpr (std::is_void_v<typename std::tuple_element<I, TTuple>::type>) {
    return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<size_t, Ints1...>{}, std::integer_sequence<size_t, Ints2...>{});
  } else {
    return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<size_t, Ints1..., I>{}, std::integer_sequence<size_t, Ints2...>{});
  }
}

template <class TTuple>
constexpr auto get_tuple_index_seq_without_voids() {
  return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<size_t>{}, std::make_index_sequence<std::tuple_size_v<TTuple>>{});
}

template <class TTuple, size_t... Ints>
constexpr std::integer_sequence<size_t, Ints...> get_tuple_index_seq_of_voids_impl(std::integer_sequence<size_t, Ints...>, std::integer_sequence<size_t>) {
  return {};
}

template <class TTuple, size_t I, size_t... Ints1, size_t... Ints2>
constexpr auto get_tuple_index_seq_of_voids_impl(std::integer_sequence<size_t, Ints1...>, std::integer_sequence<size_t, I, Ints2...>) {
  if constexpr (!std::is_void_v<typename std::tuple_element<I, TTuple>::type>) {
    return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<size_t, Ints1...>{}, std::integer_sequence<size_t, Ints2...>{});
  } else {
    return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<size_t, Ints1..., I>{}, std::integer_sequence<size_t, Ints2...>{});
  }
}

template <class TTuple>
constexpr auto get_tuple_index_seq_of_voids() {
  return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<size_t>{}, std::make_index_sequence<std::tuple_size_v<TTuple>>{});
}

template <class TAwaiter>
struct await_suspension_wrapper {
  bool await_ready() noexcept {
    return _awaiter.await_ready();
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    _suspension = h.promise().suspend(_awaiter.num_suspensions() + 1);

    _awaiter.suspend_coro_awaiter([this]() {
      _suspension.try_to_continue_from_any_thread();
    });

    _suspension.try_to_continue_immediately();
  }

  auto await_resume() {
    return _awaiter.await_resume();
  }

  TAwaiter _awaiter;
  coroutine_suspender _suspension{};
};

template <class... TAwaiters>
struct all_awaiter;

template <class... TAwaiters>
struct any_awaiter;

// wrapper for single task to await with operators || and &&
template <class TRes>
struct handle_awaiter {
  using result_type = TRes;

  explicit handle_awaiter(task_handle<TRes> handle) noexcept : _handle(std::move(handle)) {}

  template <class TRes2>
  auto operator&&(task_handle<TRes2>&& other) && noexcept {
    return all_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  template <class TRes2>
  auto operator||(task_handle<TRes2>&& other) && noexcept {
    return any_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  bool await_ready() const noexcept { return _handle.done(); }

  std::uint32_t num_suspensions() const noexcept { return 1; }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, promise_result<TRes>&>)
  void suspend_coro_awaiter(Fx&& continue_f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, promise_result<TRes>&>) {
    _handle.continue_with(std::forward<Fx>(continue_f));
  }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void>)
  void suspend_coro_awaiter(Fx&& continue_f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void>) {
    _handle.continue_with([continue_f = std::forward<Fx>(continue_f)](promise_result<TRes>&) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, void>) {
      continue_f();
    });
  }

  void reset_suspender_coro_awaiter() noexcept {
    _handle.reset_continue();
  }

  TRes await_resume() {
    return std::move(_handle).get();
  }

 private:
  task_handle<TRes> _handle;
};

// awaiter for coroutines scheduled with && op
template <class... TAwaiters>
struct all_awaiter {
  template <class...>
  friend struct all_awaiter;

  using result_type = remove_void_tuple_t<typename TAwaiters::result_type...>;

  explicit all_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator&&(TAwaiter&& other) && noexcept {
    return all_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::move(other)})};
  }

  template <class... TAwaiters2>
  auto operator&&(all_awaiter<TAwaiters2...>&& other) && noexcept {
    return all_awaiter<TAwaiters..., TAwaiters2...>{std::tuple_cat(std::move(_awaiters), std::move(other._awaiters))};
  }

  template <class TRes>
  auto operator&&(task_handle<TRes>&& other) && noexcept {
    return all_awaiter<TAwaiters..., handle_awaiter<TRes>>{std::tuple_cat(std::move(_awaiters), std::tuple<handle_awaiter<TRes>>{std::move(other)})};
  }

  // (&& ...) || (...) - case
  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator||(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return any_awaiter{std::tuple<all_awaiter, TAwaiter>{std::move(*this), std::move(other)}};
  }

  // && ... || task - case. Operator && has less precedence so its equal to (&& ...) || task case
  template <class TRes>
  auto operator||(task_handle<TRes>&& other) && noexcept {
    return any_awaiter{std::tuple<all_awaiter, handle_awaiter<TRes>>{std::move(*this), std::move(other)}};
  }

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto prepend_awaiter(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return internal::all_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::move(other)), std::move(_awaiters))};
  }

  bool await_ready() noexcept {
    return std::apply(
        [&](auto&... awaiters) {
          return (awaiters.await_ready() && ...);
        },
        _awaiters);
  }

  std::uint32_t num_suspensions() const noexcept {
    return std::apply(
        [&](const auto&... awaiters) {
          return (awaiters.num_suspensions() + ...);
        },
        _awaiters);
  }

  void reset_suspender_coro_awaiter() noexcept {
    return std::apply(
        [&](auto&... awaiters) {
          (awaiters.reset_suspender_coro_awaiter(), ...);
        },
        _awaiters);
  }

  template <class Fx>
  void suspend_coro_awaiter(const Fx& continue_f) {
    std::apply(
        [continue_f](auto&... awaiters) {
          (awaiters.suspend_coro_awaiter(continue_f), ...);
        },
        _awaiters);
  }

  result_type await_resume() {
    using voids_index_seq = decltype(get_tuple_index_seq_of_voids<std::tuple<typename TAwaiters::result_type...>>());

    if constexpr (std::is_same_v<voids_index_seq, std::integer_sequence<size_t>>) {
      [&]<size_t... Ints>(std::integer_sequence<size_t, Ints...>) {
        (std::get<Ints>(std::move(_awaiters)).await_resume(), ...);
      }(voids_index_seq{});
    }

    if constexpr (std::is_same_v<result_type, std::tuple<>>) {
      return {};
    } else {
      using result_index_seq = decltype(get_tuple_index_seq_without_voids<std::tuple<typename TAwaiters::result_type...>>());

      return [&]<size_t... Ints>(std::integer_sequence<size_t, Ints...>) -> result_type {
        return {std::get<Ints>(std::move(_awaiters)).await_resume()...};
      }(result_index_seq{});
    }
  }

  await_suspension_wrapper<all_awaiter> coro_await_transform(base_handle&) && {
    return {std::move(*this)};
  }

 private:
  std::tuple<TAwaiters...> _awaiters;
};

template <typename... TArgs>
all_awaiter(std::tuple<TArgs>...) -> all_awaiter<TArgs...>;

// awaiter for coroutines scheduled with || op
template <class... TAwaiters>
struct any_awaiter {
  static_assert(sizeof...(TAwaiters) > 1);

  template <class...>
  friend struct any_awaiter;

  using result_type = decltype(get_variant_for_types(replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit any_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  any_awaiter(any_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(other._result_index.load(std::memory_order::relaxed) == 0);
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
        return true;
      }
      return false;
    };

    return calculate_is_ready(func, std::index_sequence_for<TAwaiters...>{});
  }

  std::uint32_t num_suspensions() const noexcept { return 1; }

  void reset_suspender_coro_awaiter() noexcept {
    return std::apply(
        [&](auto&... awaiters) {
          (awaiters.reset_suspender_coro_awaiter(), ...);
        },
        _awaiters);
  }

  template <class Fx>
  void suspend_coro_awaiter(const Fx& continue_f) {
    const auto func = [&continue_f, this](auto& awaiter, auto, size_t awaiter_index) {
      awaiter.suspend_coro_awaiter([this, continue_f, awaiter_index]() {
        size_t expected_index = 0;
        if (_result_index.compare_exchange_strong(expected_index, awaiter_index, std::memory_order::relaxed)) {
          continue_f();
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

    reset_suspender_coro_awaiter();

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
};

template <typename... TArgs>
any_awaiter(std::tuple<TArgs>...) -> any_awaiter<TArgs...>;

}  // namespace async_coro::internal
