#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/always_false.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/scheduler.h>
#include <async_coro/task_handle.h>

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

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

template <size_t I, size_t... Ints, class TTuple, class TElem>
constexpr auto replace_last_tuple_elem_impl(std::integer_sequence<size_t, Ints...>, TTuple&& tuple, TElem&& new_last_elem) noexcept {
  static_assert(!std::is_reference_v<TElem>);

  using tuple_t = std::tuple<std::tuple_element<Ints, TTuple>..., TElem>;

  return tuple_t{std::get<Ints>(std::move(tuple))..., std::move(new_last_elem)};
}

template <class TTuple, class TElem>
constexpr auto replace_last_tuple_elem(TTuple&& tuple, TElem&& new_last_elem) noexcept {
  return replace_last_tuple_elem_impl(std::make_index_sequence<std::tuple_size_v<TTuple> - 1>(), std::forward<TTuple>(tuple), std::forward<TElem>(new_last_elem));
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

template <class TAwaiter>
struct await_suspension_wrapper {
  bool await_ready() {
    return _awaiter.await_ready();
  }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    _suspension = h.promise().suspend(_awaiter.num_suspensions() + 1);

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
  all_awaiter<handle_awaiter<TRes>, handle_awaiter<TRes2>> operator&&(task_handle<TRes2>&& other) && noexcept {
    return {._awaiters = {std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  template <class TRes2>
  any_awaiter<handle_awaiter<TRes>, handle_awaiter<TRes2>> operator||(task_handle<TRes2>&& other) && noexcept {
    return {._awaiters = {std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  bool await_ready() const noexcept { return _handle.done(); }

  std::uint32_t num_suspensions() const noexcept { return 1; }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, promise_result<TRes>&>)
  void suspend_coro_awaiter(Fx&& continue_f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, promise_result<TRes>&>) {
    _handle.continue_with(std::forward<Fx>(continue_f));
  }

  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, void>)
  void suspend_coro_awaiter(Fx&& continue_f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, void>) {
    _handle.continue_with([continue_f = std::forward<Fx>(continue_f)](promise_result<TRes>&) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, void>) {
      continue_f();
    });
  }

  void check_coro_exception() const {
    _handle.check_exception();
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

  // && ... || task - case
  template <class TRes>
  auto operator||(task_handle<TRes>&& other) && noexcept {
    auto& last_awaiter = std::get<sizeof...(TAwaiters) - 2>(_awaiters);

    return internal::all_awaiter{replace_last_tuple_elem(std::move(_awaiters), std::move(last_awaiter) || std::move(other))};
  }

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto prepend_awaiter(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return internal::all_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::move(other)), std::move(_awaiters))};
  }

  bool await_ready() {
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

  template <class Fx>
  void suspend_coro_awaiter(const Fx& continue_f) {
    std::apply(
        [&continue_f](auto&... awaiters) {
          (awaiters.suspend_coro_awaiter(continue_f), ...);
        },
        _awaiters);
  }

  void check_coro_exception() const {
    std::apply(
        [&](auto&... awaiters) {
          (awaiters.check_coro_exception(), ...);
        },
        _awaiters);
  }

  result_type await_resume() {
    check_coro_exception();

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
    ASYNC_CORO_ASSERT(other._has_result.load(std::memory_order::relaxed) = false);
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

  // || ... && task - case
  template <class TRes>
  auto operator&&(task_handle<TRes>&& other) && noexcept {
    auto& last_awaiter = std::get<sizeof...(TAwaiters) - 2>(_awaiters);

    return internal::any_awaiter{replace_last_tuple_elem(std::move(_awaiters), std::move(last_awaiter) && std::move(other))};
  }

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto prepend_awaiter(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return internal::any_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::move(other)), std::move(_awaiters))};
  }

  bool await_ready() {
    const auto store_result = [this](auto& awaiter, auto index) -> bool {
      if (!this->_has_result.exchange(true, std::memory_order::relaxed)) {
        using result_t = decltype(awaiter.await_resume());
        if constexpr (std::is_void_v<result_t>) {
          awaiter.await_resume();
          new (&_result) result_type{index, std::monostate{}};
        } else {
          new (&_result) result_type{index, awaiter.await_resume()};
        }
      }
      return true;
    };

    return calculate_is_ready(store_result, std::index_sequence_for<TAwaiters...>{});
  }

  std::uint32_t num_suspensions() const noexcept {
    return 1;
  }

  template <class Fx>
  void suspend_coro_awaiter(const Fx& continue_f) {
    const auto func = [&continue_f, this](auto& awaiter, auto index) {
      using result_t = decltype(awaiter.await_resume());
      using index_t = decltype(index);

      awaiter.suspend_coro_awaiter([this, &continue_f, &awaiter]() {
        if (!_has_result.exchange(true, std::memory_order::relaxed)) {
          // continue this coro
          if constexpr (std::is_void_v<result_t>) {
            new (&_result) result_type{index_t{}, std::monostate{}};

          } else {
            new (&_result) result_type{index_t{}, awaiter.await_resume()};
          }
          continue_f();
        }
      });
    };

    call_functor(func, std::index_sequence_for<TAwaiters...>{});
  }

  void check_coro_exception() const noexcept {}

  result_type await_resume() noexcept(std::is_nothrow_move_constructible_v<result_type>) {
    ASYNC_CORO_ASSERT(_has_result.load(std::memory_order::relaxed));

    return std::move(_result);
  }

  await_suspension_wrapper<any_awaiter> coro_await_transform(base_handle&) && {
    return {std::move(*this)};
  }

 private:
  template <class F, size_t... TI>
  auto calculate_is_ready(const F& store_result, std::integer_sequence<size_t, TI...>) {
    return ((std::get<TI>(_awaiters).await_ready() && store_result(std::get<TI>(_awaiters), std::in_place_index_t<TI>{})) || ...);
  }

  template <class F, size_t... TI>
  void call_functor(const F& func, std::integer_sequence<size_t, TI...>) {
    (func(std::get<TI>(_awaiters), std::in_place_index_t<TI>{}), ...);
  }

 private:
  std::tuple<TAwaiters...> _awaiters;
  union {
    result_type _result;
  };
  std::atomic_bool _has_result{false};
};

template <typename... TArgs>
any_awaiter(std::tuple<TArgs>...) -> any_awaiter<TArgs...>;

}  // namespace async_coro::internal
