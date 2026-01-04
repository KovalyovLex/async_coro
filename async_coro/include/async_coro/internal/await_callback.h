#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/thread_safety/light_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/warnings.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <typename T>
union non_initialised_result {
  non_initialised_result() noexcept {}
  non_initialised_result(const non_initialised_result&) = delete;
  non_initialised_result(non_initialised_result&&) = delete;
  non_initialised_result& operator=(const non_initialised_result&) = delete;
  non_initialised_result& operator=(non_initialised_result&&) = delete;
  ~non_initialised_result() noexcept {};

  T res;
};

struct empty_result {};

template <typename T, typename R = void>
class await_callback {
  static_assert(!std::is_reference_v<R>, "await_callback can't hold references. Use pointer or std::reference_wrapper");

 public:
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)
      : _on_cancel(this),
        _on_await(std::move(callback)) {}

  // cannot be moved as we need to keep pointer in external callback
  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  ~await_callback() noexcept = default;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return false; }

  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4702)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    auto callback = continue_callback{*this, handle.promise().get_owning_ptr()};

    this->_suspension = handle.promise().suspend(2, &this->_on_cancel);

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
    return std::move(this->_result.res);
  }

 private:
  class on_cancel_callback {
   public:
    void operator()() const {
      bool executed = false;
      {
        async_coro::unique_lock lock{callback->_mutex};
        if (callback->_continue != nullptr) {
          callback->_continue->_handle = nullptr;
          callback->_continue->_cancelled.store(true, std::memory_order::relaxed);
        } else {
          // callback was destroyed and we assume continue was called
          executed = true;
        }
      }

      if (!executed) {
        callback->_suspension.try_to_continue_from_any_thread(true);
      }
    }

    await_callback* callback;
  };

  class continue_callback {
    friend on_cancel_callback;

   public:
    continue_callback(await_callback& callback, base_handle_ptr handle) noexcept
        : _callback(std::addressof(callback)),
          _handle(std::move(handle)) {
      async_coro::unique_lock lock{callback._mutex};
      callback._continue = this;
    }

    continue_callback(const continue_callback&) = delete;
    continue_callback(continue_callback&& other) noexcept
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

    continue_callback& operator=(const continue_callback&) = delete;
    continue_callback& operator=(continue_callback&& other) = delete;

    ~continue_callback() noexcept {
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

      // store result under the continue lock (we hold _continue_mutex via callback_lock)
      std::construct_at(std::addressof(_callback->_result.res), std::forward<V>(value));

      _callback->_suspension.try_to_continue_from_any_thread(false);
    }

   private:
    base_handle_ptr try_continue() {
      base_handle_ptr handle;

      if (_cancelled.load(std::memory_order::relaxed)) {
        return handle;
      }

      {
        unique_lock lock{_callback->_mutex};

        if (_cancelled.load(std::memory_order::relaxed)) {
          return handle;
        }

        _callback->_continue = nullptr;

        handle = std::move(_handle);
      }
      return handle;
    }

   private:
    await_callback* _callback;
    base_handle_ptr _handle;
    std::atomic_bool _cancelled{false};
  };

 private:
  using stored_result_t = std::conditional_t<std::is_void_v<R>, empty_result, non_initialised_result<R>>;

  light_mutex _mutex;
  coroutine_suspender _suspension;
  continue_callback* _continue CORO_THREAD_GUARDED_BY(_mutex) = nullptr;
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  stored_result_t _result;
  T _on_await;
};

}  // namespace async_coro::internal
