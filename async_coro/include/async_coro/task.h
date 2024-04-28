#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/promise_result.h>

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
struct promise_type final : internal::promise_result<R>, base_handle {
  // construct my promise from me
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<promise_type>::from_promise(*this);
  }

  // all promises await to be started in scheduller or after embedding
  constexpr auto initial_suspend() noexcept {
    init_promise(get_return_object());
    return std::suspend_always();
  }

  // we dont want to destroy our result here
  std::suspend_always final_suspend() noexcept;

  template <typename T>
  constexpr decltype(auto) await_transform(T&& in) noexcept {
    // return non standart awaiters as is
    return std::move(in);
  }

  template <embeddable_task T>
  constexpr decltype(auto) await_transform(T&& in) {
    in.embed_task(*this);
    return std::move(in);
  }

  void set_continuation(task_handle<R>* handle, internal::passkey_any<task_handle<R>>) {
    set_continuation_impl(handle);
  }

  void try_free_task(internal::passkey_any<task<R>>) {
    try_free_task_impl();
  }
};

// Default type for all coroutines
template <typename R>
struct task final {
  using promise_type = async_coro::promise_type<R>;
  using handle_type = std::coroutine_handle<promise_type>;
  using return_type = R;

  task(handle_type h) noexcept : _handle(std::move(h)) {}

  task(const task&) = delete;
  task(task&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)) {}

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

    bool await_ready() const noexcept { return t._handle.done(); }

    template <typename T>
    void await_suspend(std::coroutine_handle<T>) const noexcept {}

    R await_resume() {
      if constexpr (!std::is_reference_v<R>) {
        R res{t._handle.promise().move_result()};
        t._handle.promise().try_free_task(internal::passkey<task>{});
        return res;
      } else {
        auto& res = t._handle.promise().get_result_ref();
        t._handle.promise().try_free_task(internal::passkey<task>{});
        return res;
      }
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

// Task handle for schedulled coroutines. Can track coroutine state and get result
template <typename R>
class task_handle final {
 public:
  using continuation_t = move_only_function<void(promise_type<R>&) noexcept>;

  task_handle() noexcept = default;
  explicit task_handle(std::coroutine_handle<async_coro::promise_type<R>> h) noexcept
      : _handle(std::move(h)) {
    ASYNC_CORO_ASSERT(_handle);
    if (_handle) [[likely]] {
      _handle.promise().set_continuation(this, internal::passkey{this});
    }
  }

  task_handle(const task_handle&) = delete;
  task_handle(task_handle&& other) noexcept
      : _handle(std::exchange(other._handle, nullptr)),
        _continuation(std::move(other._continuation)) {
    if (_handle) {
      _handle.promise().set_continuation(this, internal::passkey{this});
    }
  }

  task_handle& operator=(const task_handle&) = delete;
  task_handle& operator=(task_handle&& other) noexcept {
    if (_handle == other._handle) {
      return *this;
    }
    if (_handle) {
      _handle.promise().set_continuation(nullptr, internal::passkey{this});
    }
    _handle = std::exchange(other._handle, {});
    _continuation = std::move(other._continuation);

    if (_handle) {
      _handle.promise().set_continuation(this, internal::passkey{this});
    }

    return *this;
  }

  ~task_handle() noexcept {
    _continuation = nullptr;
    if (_handle) {
      _handle.promise().set_continuation(nullptr, internal::passkey{this});
    }
  }

  // access
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

  bool done() const { return !_handle || _handle.done(); }

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
  on_final_suspend();
  if (auto* continue_with = get_continuation<task_handle<R>>()) {
    continue_with->continue_impl(*this, internal::passkey{this});
  }
  return {};
}
}  // namespace async_coro
