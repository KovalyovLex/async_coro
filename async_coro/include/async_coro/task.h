#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/continue_function.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_type.h>
#include <async_coro/unique_function.h>

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>


namespace async_coro {

class scheduler;

struct task_base {
  void on_child_coro_added(base_handle& parent, base_handle& child);
};

/**
 * @brief Default return type for asynchronous coroutines.
 *
 * This type is used as the default return type for coroutines producing a result of type `R`.
 * It encapsulates the coroutine handle and associated promise, and defines the coroutine
 * interface expected by the compiler.
 *
 * @tparam R The result type produced by the coroutine.
 */
template <typename R>
struct task final : private task_base {
  using promise_type = async_coro::internal::promise_type<R>;
  using handle_type = std::coroutine_handle<promise_type>;
  using return_type = R;

  task(handle_type h) noexcept : _handle(std::move(h)) {}

  task(const task&) = delete;
  task(task&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)) {
  }

  task& operator=(const task&) = delete;
  task& operator=(task&& other) noexcept {
    std::swap(_handle, other._handle);
    return *this;
  }

  ~task() noexcept {
    if (_handle) {
      _handle.promise().try_free_task(internal::passkey{this});
    }
  }

  struct awaiter {
    task& t;

    awaiter(task& tas) noexcept : t(tas) {}
    awaiter(const awaiter&) = delete;
    awaiter(awaiter&&) = delete;
    ~awaiter() noexcept = default;

    bool await_ready() const noexcept { return t._handle.done(); }

    template <typename T>
      requires(std::derived_from<T, base_handle>)
    void await_suspend(std::coroutine_handle<T> h) const noexcept {
      h.promise().on_suspended();
    }

    R await_resume() {
      return t._handle.promise().move_result();
    }
  };

  // coroutine should be moved to become embedded
  auto operator co_await() && {
    return awaiter(*this);
  }

  auto operator co_await() & = delete;
  auto operator co_await() const& = delete;
  auto operator co_await() const&& = delete;

  bool done() const { return _handle.done(); }

  void embed_task(base_handle& parent) {
    on_child_coro_added(parent, _handle.promise());
  }

  handle_type release_handle(internal::passkey_successors<scheduler>) {
    return std::exchange(_handle, {});
  }

 private:
  handle_type _handle{};
};

template <class T, class... TArgs>
concept is_noexcept_runnable = requires(T a) {
  { a(std::declval<TArgs>()...) } noexcept -> std::same_as<void>;
};

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
 * To safely use a continuation, the `task_handle` must outlive the associated coroutine.
 *
 * @tparam R The result type produced by the coroutine.
 */
template <typename R>
class task_handle final {
  using promise_type = async_coro::internal::promise_type<R>;
  using handle_type = std::coroutine_handle<promise_type>;

 public:
  using continuation_t = unique_function<void(promise_result<R>&) noexcept>;

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
    ASYNC_CORO_ASSERT(_handle && _handle.promise().f);
    return _handle.promise().move_result();
  }

  // Checks if coroutine is finished.
  // If state is empty returns true as well
  bool done() const {
    return !_handle || _handle.promise().is_finished();
  }

  // Sets callback that will be called after coroutine finish
  template <class Fx>
    requires(is_noexcept_runnable<Fx, promise_result<R>&>)
  void continue_with(Fx&& f) {
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!_handle) {
      return;
    }

    if (done()) {
      f(_handle.promise());
    } else {
      struct callable : public internal::continue_function<promise_result<R>&> {
        callable(Fx&& fx) : func(std::forward<Fx>(fx)) {}

        void execute(promise_result<R>& res) noexcept override {
          func(res);
        }

        std::remove_cvref_t<Fx> func;
      };
      auto continue_f = new callable{std::forward<Fx>(f)};
      _handle.promise().set_continuation_functor(continue_f, internal::passkey{this});
    }
  }

 private:
  handle_type _handle{};
};

namespace internal {

template <typename R>
std::suspend_always promise_type<R>::final_suspend() noexcept {
  this->on_final_suspend();
  if (auto* continue_with = static_cast<internal::continue_function<promise_result<R>&>*>(this->get_continuation_functor())) {
    continue_with->execute(*this);
  }
  return {};
}

}  // namespace internal

}  // namespace async_coro
