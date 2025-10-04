#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

class await_callback_base {
 protected:
  await_callback_base() noexcept : _on_cancel(on_cancel_callback{this}) {}

  ~await_callback_base() noexcept {
    reset_continue_callback();
  }

  void reset_continue_callback() noexcept {
    bool expected = false;
    while (!_change_in_process.compare_exchange_strong(expected, true, std::memory_order::acq_rel)) {
      expected = false;
    }

    if (auto clb = _continue.exchange(nullptr, std::memory_order::relaxed)) {
      clb->change_continue(nullptr, true);
    }

    _change_in_process.store(false, std::memory_order::release);
  }

  class continue_callback {
   public:
    continue_callback(await_callback_base& c) noexcept : callback(&c) {
      c._continue.store(this, std::memory_order::relaxed);
    }
    continue_callback(const continue_callback&) = delete;
    continue_callback(continue_callback&& other) noexcept
        : callback(other.callback.load(std::memory_order::relaxed)) {
      other.change_continue(this);
    }

    continue_callback& operator=(const continue_callback&) = delete;
    continue_callback& operator=(continue_callback&& other) noexcept {
      if (this == &other) {
        return *this;
      }

      change_continue(nullptr);

      other.change_continue(this);

      return *this;
    }

    bool change_continue(continue_callback* new_ptr, bool callback_write_granted = false) noexcept {
      auto clb = callback.load(std::memory_order::relaxed);

      if (!clb) {
        if (new_ptr) {
          new_ptr->callback.store(clb, std::memory_order::relaxed);
        }
        return false;
      }

      // busy wait self
      bool expected = false;
      while (!change_in_process.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        expected = false;
      }

      // busy wait callback
      do {
        if (clb != callback.load(std::memory_order::relaxed)) {
          callback.store(nullptr, std::memory_order::relaxed);
          change_in_process.store(false, std::memory_order_release);
          return false;
        }
        expected = false;
      } while (!callback_write_granted && !clb->_change_in_process.compare_exchange_strong(expected, true, std::memory_order_relaxed));

      if (new_ptr) {
        new_ptr->callback.store(clb, std::memory_order::relaxed);
      }
      clb->_continue.store(new_ptr, std::memory_order::relaxed);
      callback.store(nullptr, std::memory_order::relaxed);

      clb->_change_in_process.store(false, std::memory_order_release);
      change_in_process.store(false, std::memory_order_release);
      return true;
    }

    ~continue_callback() noexcept {
      change_continue(nullptr);
    }

    void operator()() const {
      const auto clb = callback.load(std::memory_order::relaxed);
      if (!clb) [[unlikely]] {
        return;
      }

      // busy wait self
      bool expected = false;
      while (!change_in_process.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        expected = false;
      }

      // busy wait callback
      do {
        if (clb != callback.load(std::memory_order::relaxed)) {
          return;
        }
        expected = false;
      } while (!clb->_change_in_process.compare_exchange_strong(expected, true, std::memory_order_relaxed));

      // release self lock
      change_in_process.store(false, std::memory_order_release);

      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        // no cancel here should be done. clb cant be destroyed so we can release lock
        clb->_change_in_process.store(false, std::memory_order_release);

        clb->_suspension.try_to_continue_from_any_thread(false);
        return;
      }

      clb->_change_in_process.store(false, std::memory_order_release);
    }

    std::atomic<await_callback_base*> callback;
    mutable std::atomic_bool change_in_process = false;
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
  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void> _on_cancel;
  std::atomic<continue_callback*> _continue = nullptr;
  std::atomic_bool _was_done = false;
  std::atomic_bool _change_in_process = false;
};

template <typename T>
struct await_callback : private await_callback_base {
  explicit await_callback(T&& callback) noexcept(std::is_nothrow_constructible_v<T, T&&>)
      : _on_await(std::forward<T>(callback)) {}

  await_callback(const await_callback&) = delete;
  await_callback(await_callback&&) = delete;

  await_callback& operator=(await_callback&&) = delete;
  await_callback& operator=(const await_callback&) = delete;

  ~await_callback() noexcept = default;

  bool await_ready() const noexcept { return false; }

  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4702)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
    _was_done.store(false, std::memory_order::relaxed);

    _suspension = h.promise().suspend(2, &_on_cancel);

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
