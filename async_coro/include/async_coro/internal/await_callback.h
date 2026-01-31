#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/thread_safety/light_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/utils/callback_on_stack.h>
#include <async_coro/utils/no_unique_address.h>
#include <async_coro/utils/non_initialised_value.h>
#include <async_coro/warnings.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

struct empty_result {};

template <typename R>
class await_callback_base;

// Continue callback that passes for external call
// Can only be moved
template <typename R>
class await_continue_callback {
  friend await_callback_base<R>;

 public:
  await_continue_callback(await_callback_base<R>& callback, base_handle_ptr handle) noexcept
      : _callback(std::addressof(callback)),
        _handle(std::move(handle)) {
    async_coro::unique_lock lock{callback._mutex};
    callback._continue = this;
  }

  await_continue_callback(const await_continue_callback&) = delete;
  await_continue_callback(await_continue_callback&& other) noexcept
      : _callback(other._callback) {
    // check current callback wasn't cancelled
    if (other._handle && !other._cancelled.load(std::memory_order::relaxed)) {
      async_coro::unique_lock lock{_callback->_mutex};
      if (!other._cancelled.load(std::memory_order::relaxed)) {
        _callback->_continue = this;
        _handle = std::move(other._handle);
        return;
      }
    }
    _cancelled.store(true, std::memory_order::relaxed);
  }

  await_continue_callback& operator=(const await_continue_callback&) = delete;
  await_continue_callback& operator=(await_continue_callback&& other) = delete;

  ~await_continue_callback() noexcept {
    if (_handle) {
      async_coro::unique_lock lock{_callback->_mutex};
      if (_handle) {
        ASYNC_CORO_ASSERT(_callback->_continue == this);

        _callback->_continue = nullptr;
      }
    }
  }

  // void variant
  void operator()()
    requires std::is_void_v<R>
  {
    auto handle = try_continue();
    if (!handle) {
      return;
    }

    // continue without lock
    _callback->_suspension.try_to_continue_from_any_thread(false);
  }

  // non-void variant: accept a value to deliver to awaiting coroutine
  template <typename V>
    requires(!std::is_void_v<V> && std::is_constructible_v<R, V &&>)
  void operator()(V&& value) {
    auto handle = try_continue();
    if (!handle) {
      return;
    }

    // store result
    _callback->_result.initialize(std::forward<V>(value));

    // notify suspension
    _callback->_suspension.try_to_continue_from_any_thread(false);
  }

 private:
  base_handle_ptr try_continue() {
    base_handle_ptr handle;

    {
      unique_lock lock{_callback->_mutex};

      if (_cancelled.load(std::memory_order::relaxed)) {
        return handle;
      }

      _callback->_continue = nullptr;
      _callback->_was_continue_called = true;

      handle = std::move(_handle);
    }

    if (handle) {
      // remove cancel
      _callback->_suspension.remove_cancel_callback();
    }

    return handle;
  }

 private:
  await_callback_base<R>* _callback;
  base_handle_ptr _handle;
  std::atomic_bool _cancelled{false};
};

template <typename R>
class await_callback_base {
  friend await_continue_callback<R>;

 public:
  await_callback_base() noexcept = default;

  // cannot be moved as we need to keep pointer in external callback
  await_callback_base(const await_callback_base&) = delete;
  await_callback_base(await_callback_base&&) = delete;

  ~await_callback_base() noexcept(std::is_nothrow_destructible_v<stored_result_t>) {
    if constexpr (!std::is_void_v<R>) {
      bool need_destroy = false;
      {
        async_coro::unique_lock lock{_mutex};
        need_destroy = _was_continue_called;
      }

      if (need_destroy) {
        _result.destroy();
      }
    }
  }

  await_callback_base& operator=(await_callback_base&&) = delete;
  await_callback_base& operator=(const await_callback_base&) = delete;

 private:
  class cancel_callback : public callback_on_stack<cancel_callback, base_handle::cancel_callback> {
   public:
    void on_destroy() {
      auto& awaiter = this->get_owner(&await_callback_base::_on_cancel);

      awaiter._suspension.try_to_continue_from_any_thread(false);
    }

    void on_execute_and_destroy() {
      auto& awaiter = this->get_owner(&await_callback_base::_on_cancel);

      // cancel awaiting first
      bool executed = false;
      {
        async_coro::unique_lock lock{awaiter._mutex};
        if (awaiter._continue != nullptr) {
          // cancel execution of callback
          awaiter._continue->_handle = nullptr;
          awaiter._continue->_cancelled.store(true, std::memory_order::relaxed);
        }
        executed = awaiter._was_continue_called;
      }

      if (!executed) {
        // decrease num suspensions for continuation callback as we disarmed it
        awaiter._suspension.try_to_continue_from_any_thread(true);
      }

      // then decrease num suspensions
      awaiter._suspension.try_to_continue_from_any_thread(true);
    }
  };

 protected:
  using stored_result_t = std::conditional_t<std::is_void_v<R>, empty_result, non_initialised_value<R>>;

  coroutine_suspender _suspension;
  cancel_callback _on_cancel;
  // alive continue callback. Can be moved thats why we use mutex for synchronization
  await_continue_callback<R>* _continue CORO_THREAD_GUARDED_BY(_mutex) = nullptr;
  bool _was_continue_called CORO_THREAD_GUARDED_BY(_mutex) = false;
  light_mutex _mutex;
  ASYNC_CORO_NO_UNIQUE_ADDRESS stored_result_t _result;
};

template <typename T, typename R = void>
class await_callback : public await_callback_base<R> {
  static_assert(!std::is_reference_v<R>, "await_callback can't hold references. Use pointer or std::reference_wrapper");

 public:
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)
      : await_callback_base<R>(),
        _on_await(std::move(callback)) {}

  [[nodiscard]] bool await_ready() const noexcept { return false; }

  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4702)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    auto callback = await_continue_callback{*this, handle.promise().get_owning_ptr()};

    // cancel and continue should always be called or destroyed
    this->_suspension = handle.promise().suspend(3, this->_on_cancel.get_ptr());

    this->_on_await(std::move(callback));

    this->_suspension.try_to_continue_immediately();
  }

  ASYNC_CORO_WARNINGS_MSVC_POP

  void await_resume() const noexcept
    requires std::is_void_v<R>
  {}

  auto await_resume() noexcept
    requires(!std::is_void_v<R>)
  {
    return std::move(this->_result).get_value();
  }

 private:
  ASYNC_CORO_NO_UNIQUE_ADDRESS T _on_await;
};

}  // namespace async_coro::internal
