#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_type.h>
#include <async_coro/internal/task_handle_awaiter.h>

#include <coroutine>
#include <type_traits>
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
template <typename R = void>
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

  // Checks if coroutine is finished.
  // If state is empty returns true as well
  bool done() const {
    return !_handle || _handle.promise().is_finished_acquire();
  }

  // Sets callback that will be called after coroutine finish on thread that finished the coroutine.
  // If coroutine is already finished, the callback will be called immediately.
  template <class Fx>
    requires(internal::is_runnable<std::remove_cvref_t<Fx>, void, promise_result<R>&>)
  void continue_with(Fx&& f) noexcept(internal::is_noexcept_runnable<std::remove_cvref_t<Fx>, void, promise_result<R>&>) {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    auto& promise = _handle.promise();
    if (done()) {
      f(promise);
    } else {
      auto continue_f = callback<void, promise_result<R>&>::allocate(std::forward<Fx>(f));
      promise.set_continuation_functor(continue_f, internal::passkey{this});
      if (done()) {
        if (auto f_base = promise.get_continuation_functor(internal::passkey{this})) {
          ASYNC_CORO_ASSERT(f_base == continue_f);
          (void)f_base;
          continue_f->execute(promise);
          continue_f->destroy();
        }
      }
    }
  }

  void check_exception() const noexcept(!ASYNC_CORO_WITH_EXCEPTIONS) {
    _handle.promise().check_exception();
  }

  internal::task_handle_awaiter<R> coro_await_transform(base_handle&) && {
    return internal::task_handle_awaiter<R>{std::move(*this)};
  }

 private:
  handle_type _handle{};
};

}  // namespace async_coro
