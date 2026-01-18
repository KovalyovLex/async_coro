#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/handle_awaiter.h>
#include <async_coro/internal/promise_type.h>
#include <async_coro/utils/callback_ptr.h>
#include <async_coro/utils/passkey.h>

#include <atomic>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace async_coro {

struct transfer_ownership {};

/**
 * @brief Handle for scheduled coroutine tasks.
 *
 * Represents a handle to a coroutine task scheduled for execution. This class provides
 * unique ownership of the coroutine state for the user, while also enabling shared ownership
 * with the coroutine scheduler.
 *
 * The handle can be used to:
 * - Track the coroutine's execution state.
 * - Access the result of the coroutine once it completes.
 * - Attach a continuation function, enabling a reactive programming approach.
 *
 * @tparam R The result type produced by the coroutine.
 */
template <typename R = void>
class task_handle final {
  using promise_type = async_coro::internal::promise_type<R>;
  using handle_type = std::coroutine_handle<promise_type>;
  using callback_sig = void(promise_result<R>&, bool);

 public:
  using callback_ptr = async_coro::callback_ptr<callback_sig>;

  task_handle() noexcept = default;

  task_handle(handle_type handle, transfer_ownership /*owner*/) noexcept
      : _handle(std::move(handle)) {
    ASYNC_CORO_ASSERT(_handle);
  }

  task_handle(const task_handle&) = delete;
  task_handle(task_handle&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)) {
  }

  task_handle& operator=(const task_handle&) = delete;
  task_handle& operator=(task_handle&& other) noexcept {
    if (_handle == other._handle) {
      return *this;
    }

    if (_handle) {
      _handle.promise().set_owning_by_task_handle(false, passkey{this});
    }

    _handle = std::exchange(other._handle, {});
    return *this;
  }

  ~task_handle() noexcept {
    if (_handle) {
      _handle.promise().set_owning_by_task_handle(false, passkey{this});
    }
  }

  // access for result
  decltype(auto) get() & {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().get_result_ref();
  }

  decltype(auto) get() const& {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().get_result_cref();
  }

  decltype(auto) get() && {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().move_result();
  }

  void get() const&& = delete;

  // Checks if coroutine is finished.
  // If state is empty returns true as well
  [[nodiscard]] bool done() const {
    return !_handle || _handle.promise().is_finished_acquire();
  }

  // Sets callback that will be called after coroutine finish on thread that finished the coroutine.
  // If coroutine is already finished, the callback will be called immediately.
  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, promise_result<R>&, bool>)
  void continue_with(Fx&& func) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, promise_result<R>&, bool>) {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    promise_type& promise = _handle.promise();
    if (done()) {
      func(promise, false);
    } else {
      auto* continue_f = callback_ptr::allocate(std::forward<Fx>(func)).release();
      promise.set_continuation_functor(callback_ptr{continue_f}, passkey{this});
      auto [state, cancelled] = promise.get_coroutine_state_and_cancelled(std::memory_order::acquire);
      if (state == async_coro::coroutine_state::finished || cancelled) {
        if (auto f_base = promise.template get_continuation_functor<callback_sig>(passkey{this})) {
          ASYNC_CORO_ASSERT(f_base == continue_f);
          f_base.execute_and_destroy(promise, cancelled);
        }
      }
    }
  }

  // Sets callback that will be called after coroutine finish on thread that finished the coroutine.
  // If coroutine is already finished, the callback will be called and destroyed immediately.
  void continue_with(callback_ptr func) {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    promise_type& promise = _handle.promise();
    if (done()) {
      func.execute_and_destroy(promise, false);
    } else {
      const auto ptr = func.release();
      promise.set_continuation_functor(callback_ptr{ptr}, passkey{this});
      const auto [state, cancelled] = promise.get_coroutine_state_and_cancelled(std::memory_order::acquire);
      if (state == async_coro::coroutine_state::finished || cancelled) {
        if (auto f_base = promise.template get_continuation_functor<callback_sig>(passkey{this})) {
          ASYNC_CORO_ASSERT(f_base == ptr);
          f_base.execute_and_destroy(promise, cancelled);
        }
      };
    }
  }

  // Resets continuation callback.
  void reset_continue() noexcept {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    auto& promise = _handle.promise();
    promise.set_continuation_functor(nullptr, passkey{this});
  }

  /**
   * @brief Requests coroutine to stop. Stop will happen on next suspension point of coroutine.
   *
   * @return previous state of cancel request
   */
  bool request_cancel() noexcept {
    if (!_handle) {
      return false;
    }

    auto& promise = _handle.promise();
    return promise.request_cancel();
  }

  bool is_cancelled() noexcept {
    if (!_handle) {
      return false;
    }

    return _handle.promise().is_cancelled();
  }

  auto coro_await_transform(base_handle& handle) && {
    return internal::handle_awaiter<R>{std::move(*this)}.coro_await_transform(handle);
  }

 private:
  handle_type _handle{};
};

template <class R>
auto adv_await_transform(task_handle<R>&& task) noexcept {
  return internal::handle_awaiter<R>{std::move(task)};
}

}  // namespace async_coro
