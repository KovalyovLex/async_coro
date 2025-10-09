#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/await_suspension_wrapper.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/tuple_variant_helpers.h>
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
class any_awaiter_continue_callback : public continue_callback {
 public:
  explicit any_awaiter_continue_callback(TAwaiter& awaiter) noexcept
      : continue_callback(&executor, &deleter),
        _awaiter(&awaiter) {}

  any_awaiter_continue_callback(const any_awaiter_continue_callback&) = delete;
  any_awaiter_continue_callback(any_awaiter_continue_callback&&) noexcept = default;

  ~any_awaiter_continue_callback() noexcept = default;

  any_awaiter_continue_callback& operator=(const any_awaiter_continue_callback&) = delete;
  any_awaiter_continue_callback& operator=(any_awaiter_continue_callback&&) = delete;

 private:
  static continue_callback::return_type executor(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, bool cancelled) {
    ASYNC_CORO_ASSERT(with_destroy);

    return static_cast<any_awaiter_continue_callback*>(base)->_awaiter->on_continue(cancelled, I);
  }

  static void deleter(callback_base* base) noexcept {
    static_cast<any_awaiter_continue_callback*>(base)->_awaiter->on_continuation_freed();
  }

 private:
  TAwaiter* _awaiter;
};

// awaiter for coroutines scheduled with || op
template <class... TAwaiters>
class any_awaiter {
  static_assert(sizeof...(TAwaiters) > 1);

  template <class...>
  friend class any_awaiter;

 public:
  using result_type = decltype(get_variant_for_types(replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit any_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept
      : _awaiters(std::move(awaiters)) {}

  any_awaiter(const any_awaiter&) = delete;
  any_awaiter(any_awaiter&& other) noexcept
      : _awaiters(std::move(other._awaiters)) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
  }

  ~any_awaiter() noexcept {
    // wee need to wait
    auto count = _num_await_free.load(std::memory_order::relaxed);
    while (count != 0) {
      _num_await_free.wait(count, std::memory_order::relaxed);
      count = _num_await_free.load(std::memory_order::relaxed);
    }
  }

  any_awaiter& operator=(const any_awaiter&) = delete;
  any_awaiter& operator=(any_awaiter&&) = delete;

  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator||(TAwaiter&& other) && noexcept {
    return any_awaiter<TAwaiters..., TAwaiter>{std::tuple_cat(std::move(_awaiters), std::tuple<TAwaiter>{std::forward<TAwaiter>(other)})};
  }

  template <class TRes>
  auto operator||(task_handle<TRes>&& other) && noexcept {
    return any_awaiter<TAwaiters..., handle_awaiter<TRes>>{std::tuple_cat(std::move(_awaiters), std::tuple<handle_awaiter<TRes>>{std::move(other)})};
  }

  template <class... TAwaiters2>
  // NOLINTNEXTLINE(*-param-not-moved)
  auto operator||(any_awaiter<TAwaiters2...>&& other) && noexcept {
    return any_awaiter<TAwaiters..., TAwaiters2...>{std::tuple_cat(std::move(_awaiters), std::move(other._awaiters))};
  }

  // (|| ...) && (...) - case
  template <class TAwaiter>
    requires(std::is_rvalue_reference_v<TAwaiter &&>)
  auto operator&&(TAwaiter&& other) && noexcept {
    static_assert(!std::is_reference_v<TAwaiter>);

    return all_awaiter{std::tuple<any_awaiter, TAwaiter>{std::move(*this), std::forward<TAwaiter>(other)}};
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

    return internal::any_awaiter{std::tuple_cat(std::tuple<TAwaiter>(std::forward<TAwaiter>(other)), std::move(_awaiters))};
  }

  bool await_ready() noexcept {
    const auto func = [this](std::size_t awaiter_index) noexcept {
      std::uint32_t expected_index = 0;
      if (_result_index.compare_exchange_strong(expected_index, std::uint32_t(awaiter_index + 1), std::memory_order::relaxed)) {
        // cancel other coroutines
        call_functor_while_true(
            [&](auto& awaiter_to_cancel, auto, std::size_t index) {
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

  void cancel_await() {
    std::apply(
        [&](auto&... awaiters) {
          (awaiters.cancel_await(), ...);
        },
        _awaiters);

    if (!_was_continued.exchange(true, std::memory_order::acquire)) {
      continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
      bool cancel = true;

      while (continuation) {
        std::tie(continuation, cancel) = continuation.release()->execute_and_destroy(cancel);
      }
    }
  }

  void continue_after_complete(continue_callback& continue_f) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _continue_f = &continue_f;
    _was_continued.store(false, std::memory_order::release);

    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      const auto set_continue = [&](auto& awaiter, continue_callback& callback) {
        _num_await_free.fetch_add(1, std::memory_order::relaxed);
        awaiter.continue_after_complete(callback);
        return _result_index.load(std::memory_order::relaxed) == 0;
      };

      // iterate while _result_index is zero
      (void)(set_continue(std::get<TI>(_awaiters), std::get<TI>(_callbacks)) && ...);
    };

    iter_awaiters(std::index_sequence_for<TAwaiters...>{});
  }

  continue_callback::return_type on_continue(bool cancelled, std::size_t awaiter_index) {
    std::uint32_t expected_index = 0;
    if (_result_index.compare_exchange_strong(expected_index, std::uint32_t(awaiter_index + 1), std::memory_order::relaxed)) {
      // cancel other coroutines
      call_functor_while_true(
          [&](auto& awaiter_to_cancel, auto, std::size_t index) {
            if (index != awaiter_index) {
              awaiter_to_cancel.cancel_await();
            }
            return true;
          },
          std::index_sequence_for<TAwaiters...>{});

      // continue execution
      if (!_was_continued.exchange(true, std::memory_order::relaxed)) {
        continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
        ASYNC_CORO_ASSERT(continuation);

        on_continuation_freed();
        return {std::move(continuation), cancelled};
      }
    }

    on_continuation_freed();
    return {nullptr, false};
  }

  void on_continuation_freed() noexcept {
    if (_num_await_free.fetch_sub(1, std::memory_order::relaxed) == 1) {
      _num_await_free.notify_one();
    }
  }

  result_type await_resume() {
    union_res res;
    std::size_t index = _result_index.load(std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(index > 0);
    --index;

    const auto func = [&](auto& awaiter, auto variant_index, std::size_t awaiter_index) {
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

  auto coro_await_transform(base_handle& /*handle*/) && {
    return await_suspension_wrapper<any_awaiter>{std::move(*this)};
  }

 private:
  template <class F, std::size_t... TI>
  auto calculate_is_ready(const F& store_result, std::integer_sequence<std::size_t, TI...> /*seq*/) noexcept {
    return ((std::get<TI>(_awaiters).await_ready() && store_result(TI)) || ...);
  }

  template <class F, std::size_t... TI>
  void call_functor_while_true(const F& func, std::integer_sequence<std::size_t, TI...> /*seq*/) {
    ((func(std::get<TI>(_awaiters), std::in_place_index_t<TI>{}, TI)) && ...);
  }

  static auto create_callbacks(any_awaiter& await) noexcept {
    const auto iter_awaiters = [&]<std::size_t... TI>(std::index_sequence<TI...>) {
      return std::make_tuple(any_awaiter_continue_callback<TI, any_awaiter>{await}...);
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
  using TCallbacks = decltype(create_callbacks(std::declval<any_awaiter&>()));

  std::tuple<TAwaiters...> _awaiters;
  TCallbacks _callbacks = create_callbacks(*this);
  continue_callback* _continue_f = nullptr;
  std::atomic_uint32_t _result_index{0};  // 1 based index
  std::atomic_uint32_t _num_await_free{0};
  std::atomic_bool _was_continued{false};
};

template <typename... TArgs>
any_awaiter(std::tuple<TArgs...>&&) -> any_awaiter<TArgs...>;

}  // namespace async_coro::internal
