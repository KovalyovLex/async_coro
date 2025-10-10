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
    bool expected = false;
    while (!_continue_lock.compare_exchange_strong(expected, true, std::memory_order::acq_rel)) {
      expected = false;
    }

    if (auto* clb = _continue.exchange(nullptr, std::memory_order::relaxed)) {
      clb->change_continue(nullptr, true);
    }

    _continue_lock.store(false, std::memory_order::release);
  }

  // This callback will be handled externally from any thread.
  // As we dont want to use any extra allocation we use double lock technic to sync access between await_callback and this external callback.
  class continue_callback {
   public:
    explicit continue_callback(await_callback_base& clb) noexcept : callback(&clb) {
      clb._continue.store(this, std::memory_order::relaxed);
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
      // busy wait self
      bool expected = false;
      while (!callback_lock.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        expected = false;
      }

      auto* clb = callback.load(std::memory_order::relaxed);

      if (clb == nullptr) {
        if (new_ptr != nullptr) {
          new_ptr->callback.store(nullptr, std::memory_order::relaxed);
        }
        callback_lock.store(false, std::memory_order_release);
        return false;
      }

      // busy wait callback
      do {  // NOLINT(*do-while)
        if (clb != callback.load(std::memory_order::relaxed)) {
          callback.store(nullptr, std::memory_order::relaxed);
          callback_lock.store(false, std::memory_order_release);
          return false;
        }
        expected = false;
      } while (!callback_write_granted && !clb->_continue_lock.compare_exchange_strong(expected, true, std::memory_order_relaxed));

      if (new_ptr != nullptr) {
        new_ptr->callback.store(clb, std::memory_order::relaxed);
      }
      clb->_continue.store(new_ptr, std::memory_order::relaxed);
      callback.store(nullptr, std::memory_order::relaxed);

      clb->_continue_lock.store(false, std::memory_order_release);
      callback_lock.store(false, std::memory_order_release);
      return true;
    }

    ~continue_callback() noexcept {
      change_continue(nullptr);
    }

    void operator()() const {
      auto* const clb = callback.load(std::memory_order::relaxed);
      if (clb == nullptr) [[unlikely]] {
        return;
      }

      // busy wait self
      bool expected = false;
      while (!callback_lock.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        expected = false;
      }

      // busy wait callback
      do {  // NOLINT(*do-while)
        if (clb != callback.load(std::memory_order::relaxed)) {
          return;
        }
        expected = false;
      } while (!clb->_continue_lock.compare_exchange_strong(expected, true, std::memory_order_relaxed));

      // release self lock
      callback_lock.store(false, std::memory_order_release);

      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        // no cancel here should happen. clb can't be destroyed at this point so we can release the lock
        clb->_continue_lock.store(false, std::memory_order_release);

        clb->_suspension.try_to_continue_from_any_thread(false);
        return;
      }

      clb->_continue_lock.store(false, std::memory_order_release);
    }

    std::atomic<await_callback_base*> callback;
    mutable std::atomic_bool callback_lock = false;
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
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  std::atomic<continue_callback*> _continue = nullptr;
  std::atomic_bool _was_done = false;
  std::atomic_bool _continue_lock = false;
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
