#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_result_holder.h>
#include <async_coro/unique_function.h>

#include <concepts>
#include <coroutine>
#include <utility>

namespace async_coro {

template <typename R>
class task_handle;

template <typename R>
struct task;

class scheduler;

template <typename T>
concept embeddable_task =
    requires(T a) { a.embed_task(std::declval<base_handle&>()); };

template <typename R>
struct promise_type final : internal::promise_result_holder<R> {
  // construct my promise from me
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  // all promises await to be started in scheduller or after embedding
  constexpr auto initial_suspend() noexcept {
    this->init_promise(get_return_object());
    return std::suspend_always();
  }

  // we dont want to destroy our result here
  std::suspend_always final_suspend() noexcept;

  template <typename T>
  constexpr decltype(auto) await_transform(T&& in) noexcept {
    // return non standard awaiters as is
    return std::move(in);
  }

  template <embeddable_task T>
  constexpr decltype(auto) await_transform(T&& in) {
    in.embed_task(*this);
    return std::move(in);
  }

  void set_task_handle(task_handle<R>* handle, internal::passkey_any<task_handle<R>>) {
    this->set_task_handle_impl(handle);
  }

  void try_free_task(internal::passkey_any<task<R>>) {
    this->try_free_task_impl();
  }
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
struct task final {
  using promise_type = async_coro::promise_type<R>;
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
    void await_suspend(std::coroutine_handle<T>) const noexcept {}

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
    parent.on_child_coro_added(_handle.promise());
  }

  handle_type release_handle(internal::passkey_successors<scheduler>) {
    return std::exchange(_handle, {});
  }

 private:
  handle_type _handle{};
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
 public:
  using continuation_t = unique_function<void(promise_result<R>&) noexcept>;

  task_handle() noexcept = default;
  explicit task_handle(std::coroutine_handle<async_coro::promise_type<R>> h) noexcept
      : _handle(std::move(h)) {
    ASYNC_CORO_ASSERT(_handle);
    if (_handle) [[likely]] {
      _handle.promise().set_task_handle(this, internal::passkey{this});
    }
  }

  task_handle(const task_handle&) = delete;
  task_handle(task_handle&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)),
        _continuation(std::move(other._continuation)) {
    if (_handle) {
      _handle.promise().set_task_handle(this, internal::passkey{this});
    }
  }

  task_handle& operator=(const task_handle&) = delete;
  task_handle& operator=(task_handle&& other) noexcept {
    if (_handle == other._handle) {
      return *this;
    }
    if (_handle) {
      _handle.promise().set_task_handle(nullptr, internal::passkey{this});
    }
    _handle = std::exchange(other._handle, {});
    _continuation = std::move(other._continuation);

    if (_handle) {
      _handle.promise().set_task_handle(this, internal::passkey{this});
    }

    return *this;
  }

  ~task_handle() noexcept {
    _continuation = nullptr;
    if (_handle) {
      _handle.promise().set_task_handle(nullptr, internal::passkey{this});
    }
  }

  // access for result
  decltype(auto) get() & {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().get_result_ref();
  }

  decltype(auto) get() const& {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().get_result_cref();
  }

  decltype(auto) get() && {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().move_result();
  }

  void get() const&& = delete;

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator Y&() & {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().get_result_ref();
  }

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator const Y&() const& {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().get_result_cref();
  }

  template <typename Y>
    requires(std::same_as<Y, R> && !std::same_as<R, void>)
  operator Y() && {
    ASYNC_CORO_ASSERT(_handle && _handle.done());
    return _handle.promise().move_result();
  }

  // Checks if coroutine is finished.
  // If state is empty returns true as well
  bool done() const { return !_handle || _handle.done(); }

  // Sets callback that will be called after coroutine finish
  void continue_with(continuation_t f) {
    ASYNC_CORO_ASSERT(!_continuation);
    ASYNC_CORO_ASSERT(f);
    ASYNC_CORO_ASSERT(!_handle.promise().is_coro_embedded());

    if (!f) {
      return;
    }

    if (done()) {
      f(_handle.promise());
    } else {
      _continuation = std::move(f);
    }
  }

  void continue_impl(promise_type<R>& promise, internal::passkey_any<promise_type<R>>) noexcept {
    const auto f = std::exchange(_continuation, {});
    if (f) {
      f(promise);
    }
  }

 private:
  std::coroutine_handle<async_coro::promise_type<R>> _handle{};
  continuation_t _continuation;
};

template <typename R>
std::suspend_always promise_type<R>::final_suspend() noexcept {
  this->on_final_suspend();
  if (auto* continue_with = this->template get_task_handle<task_handle<R>>()) {
    continue_with->continue_impl(*this, internal::passkey{this});
  }
  return {};
}

}  // namespace async_coro
