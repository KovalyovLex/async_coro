#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_type.h>
#include <async_coro/unique_function.h>

#include <concepts>
#include <coroutine>
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

  bool done() const noexcept { return _handle.done(); }

  void embed_task(base_handle& parent) {
    on_child_coro_added(parent, _handle.promise());
  }

  handle_type release_handle(internal::passkey_successors<scheduler>) noexcept {
    return std::exchange(_handle, {});
  }

 private:
  handle_type _handle{};
};

}  // namespace async_coro
