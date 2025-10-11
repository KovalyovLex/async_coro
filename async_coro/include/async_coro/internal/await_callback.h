#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/light_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

class await_callback_base {
 public:
  await_callback_base(const await_callback_base&) = delete;
  await_callback_base(await_callback_base&&) = delete;

  await_callback_base& operator=(await_callback_base&&) = delete;
  await_callback_base& operator=(const await_callback_base&) = delete;

 protected:
  await_callback_base() noexcept
      : _on_cancel(on_cancel_callback{this}) {
  }

  ~await_callback_base() noexcept {
    reset_continue_callback();
  }

  void reset_continue_callback() noexcept {
    unique_lock lock{_continue_mutex};

    if (auto* clb = std::exchange(_continue, nullptr)) {
      unique_lock lock_callback{clb->_callback_mutex};

      ASYNC_CORO_ASSERT(clb->_callback == this);

      auto call_no_safe = [](continue_callback* ptr) CORO_THREAD_NO_THREAD_SAFETY_ANALYSIS {
        ptr->change_continue_locked(nullptr);
      };

      call_no_safe(clb);
    }
  }

  // This callback will be handled externally from any thread.
  // As we dont want to use any extra allocation we use double lock technic to sync access between await_callback and this external callback.
  class continue_callback {
    friend class await_callback_base;

   public:
    explicit continue_callback(await_callback_base& clb) noexcept CORO_THREAD_EXCLUDES(clb._continue_mutex)
        : _callback(&clb) {
      unique_lock callback_lock{clb._continue_mutex};
      clb._continue = this;
    }
    continue_callback(const continue_callback&) = delete;
    continue_callback(continue_callback&& other) noexcept
        : _callback(other._callback) {
      other.change_continue_no_lock(this);
    }

    continue_callback& operator=(const continue_callback&) = delete;
    continue_callback& operator=(continue_callback&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      change_continue_no_lock(nullptr);

      other.change_continue_no_lock(this);

      return *this;
    }

    ~continue_callback() noexcept {
      change_continue_no_lock(nullptr);
    }

    void operator()() const {
      unique_lock callback_lock{_callback_mutex};

      if (_callback == nullptr) [[unlikely]] {
        return;
      }

      unique_lock continue_lock{_callback->_continue_mutex};

      if (!_callback->_was_done.exchange(true, std::memory_order::relaxed)) {
        auto* clb = _callback;

        // no cancel here should happen. clb can't be destroyed at this point so we can release the lock
        continue_lock.unlock();
        callback_lock.unlock();

        clb->_suspension.try_to_continue_from_any_thread(false);
        return;
      }
    }

   private:
    bool change_continue_no_lock(continue_callback* new_ptr) noexcept CORO_THREAD_EXCLUDES(_callback_mutex, new_ptr->_callback_mutex, _callback->_continue_mutex) {
      unique_lock callback_lock{_callback_mutex};

      if (new_ptr != nullptr) {
        unique_lock new_ptr_lock{new_ptr->_callback_mutex};

        if (_callback == nullptr) {
          new_ptr->_callback = nullptr;
          return false;
        }

        unique_lock clb_lock = unique_lock{_callback->_continue_mutex};

        new_ptr->_callback = _callback;
        _callback->_continue = new_ptr;
        _callback = nullptr;
      } else {
        if (_callback == nullptr) {
          return false;
        }

        unique_lock clb_lock = unique_lock{_callback->_continue_mutex};

        _callback->_continue = new_ptr;
        _callback = nullptr;
      }

      return true;
    }

    bool change_continue_locked(continue_callback* new_ptr) noexcept CORO_THREAD_EXCLUDES(new_ptr->_callback_mutex) CORO_THREAD_REQUIRES(_callback_mutex, _callback->_continue_mutex) {
      if (new_ptr != nullptr) {
        unique_lock new_ptr_lock{new_ptr->_callback_mutex};

        if (_callback == nullptr) {
          new_ptr->_callback = nullptr;
          return false;
        }

        new_ptr->_callback = _callback;
        _callback->_continue = new_ptr;
        _callback = nullptr;
      } else {
        if (_callback == nullptr) {
          return false;
        }

        _callback->_continue = new_ptr;
        _callback = nullptr;
      }

      return true;
    }

   private:
    await_callback_base* _callback CORO_THREAD_GUARDED_BY(_callback_mutex);
    mutable light_mutex _callback_mutex;
  };

  class on_cancel_callback {
   public:
    void operator()() const {
      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        // cancel
        clb->reset_continue_callback();

        clb->_suspension.try_to_continue_from_any_thread(true);
      }
    }

    await_callback_base* clb;
  };

 protected:
  std::atomic_bool _was_done = false;
  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  continue_callback* _continue CORO_THREAD_GUARDED_BY(_continue_mutex) = nullptr;
  light_mutex _continue_mutex;
};

template <typename T>
class await_callback : private await_callback_base {
 public:
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)  // NOLINT(*-not-moved)
      : _on_await(std::forward<T>(callback)) {}

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
    _was_done.store(false, std::memory_order::relaxed);

    _suspension = handle.promise().suspend(2, &_on_cancel);

    _on_await(continue_callback{*this});

    _suspension.try_to_continue_immediately();
  }

  ASYNC_CORO_WARNINGS_MSVC_POP

  void await_resume() const noexcept {}

 private:
  T _on_await;
};

template <typename T>
await_callback(T&&) -> await_callback<T>;

}  // namespace async_coro::internal
