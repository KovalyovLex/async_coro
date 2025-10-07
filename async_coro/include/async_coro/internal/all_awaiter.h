#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/await_suspension_wrapper.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/remove_void_tuple.h>
#include <async_coro/internal/tuple_variant_helpers.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <tuple>
#include <utility>

namespace async_coro {

template <class R>
class task_handle;

}  // namespace async_coro

namespace async_coro::internal {

template <std::size_t I, class TAwaiter>
class all_awaiter_continue_callback : public continue_callback {
 public:
  explicit all_awaiter_continue_callback(TAwaiter& awaiter) noexcept : continue_callback(&executor, &deleter), _awaiter(&awaiter) {}

  all_awaiter_continue_callback(const all_awaiter_continue_callback&) = delete;
  all_awaiter_continue_callback(all_awaiter_continue_callback&&) noexcept = default;
  ~all_awaiter_continue_callback() noexcept = default;

  all_awaiter_continue_callback& operator=(const all_awaiter_continue_callback&) = delete;
  all_awaiter_continue_callback& operator=(all_awaiter_continue_callback&&) = delete;

 private:
  static continue_callback::return_type executor(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, bool cancelled) {
    ASYNC_CORO_ASSERT(with_destroy);

    return static_cast<all_awaiter_continue_callback*>(base)->_awaiter->on_continue(cancelled, I);
  }

  static void deleter(callback_base* base) noexcept {
    static_cast<all_awaiter_continue_callback*>(base)->_awaiter->on_continuation_freed();
  }

  TAwaiter* _awaiter;
};

// awaiter for coroutines scheduled with && op
template <class... TAwaiters>
class all_awaiter {
  template <class...>
  friend class all_awaiter;

 public:
  using result_type = remove_void_tuple_t<typename TAwaiters::result_type...>;

  explicit all_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept : _awaiters(std::move(awaiters)) {}

  all_awaiter(const all_awaiter&) = delete;
  all_awaiter(all_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
    ASYNC_CORO_ASSERT(_num_await_free.load(std::memory_order::relaxed) == 0);
  }

  ~all_awaiter() noexcept {
    // wee need to wait
    auto count = _num_await_free.load(std::memory_order::relaxed);

    while (count != 0) {
      _num_await_free.wait(count, std::memory_order::relaxed);
      count = _num_await_free.load(std::memory_order::relaxed);
    }
  }

  all_awaiter& operator=(const all_awaiter&) = delete;
  all_awaiter& operator=(all_awaiter&&) = delete;

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator&&(TAwaiter&& other) && noexcept {
    return all_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::forward<TAwaiter>(other)})};
  }

  template <class... TAwaiters2>
  auto operator&&(all_awaiter<TAwaiters2...>&& other) && noexcept {  // NOLINT(*-not-moved)
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

    return any_awaiter{std::tuple<all_awaiter, TAwaiter>{std::move(*this), std::forward<TAwaiter>(other)}};
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

    return internal::all_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::forward<TAwaiter>(other)), std::move(_awaiters))};
  }

  bool await_ready() noexcept {
    return std::apply(
        [&](auto&... awaiters) {
          return (awaiters.await_ready() && ...);
        },
        _awaiters);
  }

  void cancel_await() noexcept {
    _was_any_cancelled.store(true, std::memory_order::relaxed);

    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_await(), ...);
        },
        _awaiters);

    if (!_was_continued.exchange(true, std::memory_order::relaxed)) {
      continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
      bool cancel = true;

      while (continuation) {
        std::tie(continuation, cancel) = continuation.release()->execute_and_destroy(cancel);
      }
    }
  }

  void continue_after_complete(continue_callback& continue_f) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _was_continued.store(false, std::memory_order::relaxed);
    _continue_f = &continue_f;
    _num_not_finished.store(std::uint32_t(sizeof...(TAwaiters)), std::memory_order::relaxed);
    _num_await_free.store(std::uint32_t(sizeof...(TAwaiters)), std::memory_order::relaxed);

    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      const auto set_continue = [&](auto& awaiter, continue_callback& callback) {
        awaiter.continue_after_complete(callback);
      };

      (set_continue(std::get<TI>(_awaiters), std::get<TI>(_callbacks)), ...);
    };

    iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  }

  continue_callback::return_type on_continue(bool cancelled, std::size_t awaiter_index) {
    if (cancelled && !_was_any_cancelled.exchange(true, std::memory_order::relaxed)) {
      // immediately notify others to cancel
      const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
        const auto cancel_awaiter = [&](auto& awaiter, std::size_t index) {
          if (index != awaiter_index) {
            awaiter.cancel_await();
          }
        };

        (cancel_awaiter(std::get<TI>(_awaiters), TI), ...);
      };

      iter_awaiters(std::index_sequence_for<TAwaiters...>{});
    }

    if (_num_not_finished.fetch_sub(1, std::memory_order::relaxed) == 1) {
      if (!_was_continued.exchange(true, std::memory_order::relaxed)) {
        continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
        ASYNC_CORO_ASSERT(continuation);

        on_continuation_freed();
        return {std::move(continuation), _was_any_cancelled.load(std::memory_order::relaxed)};
      }
    }

    on_continuation_freed();
    return {nullptr, cancelled};
  }

  void on_continuation_freed() noexcept {
    if (_num_await_free.fetch_sub(1, std::memory_order::relaxed) == 1) {
      _num_await_free.notify_one();
    }
  }

  result_type await_resume() {
    using voids_index_seq = decltype(get_tuple_index_seq_of_voids<std::tuple<typename TAwaiters::result_type...>>());

    if constexpr (!std::is_same_v<voids_index_seq, std::integer_sequence<std::size_t>>) {
      [&]<std::size_t... Ints>(std::integer_sequence<std::size_t, Ints...>) {
        (std::get<Ints>(std::move(_awaiters)).await_resume(), ...);
      }(voids_index_seq{});
    }

    if constexpr (std::is_same_v<result_type, std::tuple<>>) {
      return {};
    } else {
      using result_index_seq = decltype(get_tuple_index_seq_without_voids<std::tuple<typename TAwaiters::result_type...>>());

      return [&]<std::size_t... Ints>(std::integer_sequence<std::size_t, Ints...>) -> result_type {
        return {std::get<Ints>(std::move(_awaiters)).await_resume()...};
      }(result_index_seq{});
    }
  }

  await_suspension_wrapper<all_awaiter> coro_await_transform(base_handle& /*handle*/) && {
    return {std::move(*this)};
  }

  static auto create_callbacks(all_awaiter& await) noexcept {
    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      return std::make_tuple(all_awaiter_continue_callback<TI, all_awaiter>{await}...);
    };

    return iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  };

 private:
  using TCallbacks = decltype(create_callbacks(std::declval<all_awaiter&>()));

  std::tuple<TAwaiters...> _awaiters;
  TCallbacks _callbacks = create_callbacks(*this);
  continue_callback* _continue_f = nullptr;
  std::atomic_uint32_t _num_not_finished{0};
  std::atomic_uint32_t _num_await_free{0};
  std::atomic_bool _was_any_cancelled{false};
  std::atomic_bool _was_continued{false};
};

template <typename... TArgs>
all_awaiter(std::tuple<TArgs...>&&) -> all_awaiter<TArgs...>;

}  // namespace async_coro::internal
