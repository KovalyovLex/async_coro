#pragma once

#include <async_coro/internal/await_suspension_wrapper.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/internal/tuple_variant_helpers.h>
#include <async_coro/unique_function.h>

#include <algorithm>
#include <atomic>
#include <tuple>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

// awaiter for coroutines scheduled with && op
template <class... TAwaiters>
struct all_awaiter {
  template <class...>
  friend struct all_awaiter;

  using continue_function_t = async_coro::unique_function<void(bool), sizeof(void*) * 4>;

  using result_type = remove_void_tuple_t<typename TAwaiters::result_type...>;

  explicit all_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  all_awaiter(all_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(!other._continue_f);
  }

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

  void cancel_await() noexcept {
    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_await(), ...);
        },
        _awaiters);
  }

  void continue_after_complete(continue_function_t continue_f) {
    _continue_f = std::move(continue_f);
    _num_not_finished.store(sizeof...(TAwaiters), std::memory_order::relaxed);

    std::apply(
        [this](auto&... awaiter) {
          (awaiter.continue_after_complete([this, awaiter_ptr = static_cast<void*>(&awaiter)](bool cancelled) {
            if (cancelled && !_was_any_cancelled.exchange(true, std::memory_order::relaxed)) {
              // immediately notify others to cancel
              std::apply(
                  [&](auto&... awaiter_to_cancel) {
                    (
                        // call lambda with if
                        [awaiter_ptr](auto& awt) {
                          if (static_cast<void*>(&awt) != awaiter_ptr) {
                            awt.cancel_await();
                          }
                        }(awaiter_to_cancel),
                        ...);
                  },
                  _awaiters);
            }
            if (_num_not_finished.fetch_sub(1, std::memory_order::relaxed) == 1) {
              _continue_f(_was_any_cancelled.load(std::memory_order::relaxed));
            }
          }),
           ...);
        },
        _awaiters);
  }

  result_type await_resume() {
    using voids_index_seq = decltype(get_tuple_index_seq_of_voids<std::tuple<typename TAwaiters::result_type...>>());

    if constexpr (!std::is_same_v<voids_index_seq, std::integer_sequence<size_t>>) {
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
  continue_function_t _continue_f;
  std::atomic<size_t> _num_not_finished{0};
  std::atomic_bool _was_any_cancelled{false};
};

template <typename... TArgs>
all_awaiter(std::tuple<TArgs>...) -> all_awaiter<TArgs...>;

}  // namespace async_coro::internal