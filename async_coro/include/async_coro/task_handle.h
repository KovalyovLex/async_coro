#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_type.h>

#include <concepts>
#include <coroutine>
#include <utility>

namespace async_coro {

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
template <typename R>
class task_handle final {
  using promise_type = async_coro::internal::promise_type<R>;
  using handle_type = std::coroutine_handle<promise_type>;

 public:
  task_handle() noexcept = default;
  explicit task_handle(handle_type h) noexcept
      : _handle(std::move(h)) {
    ASYNC_CORO_ASSERT(_handle);
    if (_handle) [[likely]] {
      _handle.promise().set_owning_by_task_handle(true, internal::passkey{this});
    }
  }

  task_handle(const task_handle&) = delete;
  task_handle(task_handle&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)) {
  }

  task_handle& operator=(const task_handle&) = delete;
  task_handle& operator=(task_handle&& other) noexcept {
    if (&other == this) {
      return *this;
    }

    if (_handle) {
      _handle.promise().set_owning_by_task_handle(false, internal::passkey{this});
    }

    _handle = std::exchange(other._handle, {});
    return *this;
  }

  ~task_handle() noexcept {
    if (_handle) {
      _handle.promise().set_owning_by_task_handle(false, internal::passkey{this});
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

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator Y&() & {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().get_result_ref();
  }

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator const Y&() const& {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().get_result_cref();
  }

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator Y() && {
    ASYNC_CORO_ASSERT(_handle && _handle.promise().is_finished());
    return _handle.promise().move_result();
  }

  // Checks if coroutine is finished.
  // If state is empty returns true as well
  bool done() const {
    return !_handle || _handle.promise().is_finished_acquire();
  }

  // Sets callback that will be called after coroutine finish on thread that finished the coroutine.
  // If coroutine is already finished, the callback will be called immediately.
  template <class Fx>
    requires(internal::is_noexcept_runnable<Fx, void, promise_result<R>&>)
  void continue_with(Fx&& f) {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    if (done()) {
      f(_handle.promise());
    } else {
      auto continue_f = callback_noexcept<void, promise_result<R>&>::allocate(std::forward<Fx>(f));
      _handle.promise().set_continuation_functor(continue_f, internal::passkey{this});
    }
  }

 private:
  handle_type _handle{};
};

}  // namespace async_coro
