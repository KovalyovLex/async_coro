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

template <typename R = void>
class await_callback_base {
  static_assert(!std::is_reference_v<R>, "await_callback can't hold references. Use pointer or std::reference_wrapper");

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

    if constexpr (!std::is_void_v<R>) {
      if (_has_result) {
        std::destroy_at(std::addressof(_result.res));
      }
    }
  }

  void reset_continue_callback() noexcept CORO_THREAD_EXCLUDES(_continue_mutex) {
    _is_dying.store(true, std::memory_order::release);

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

  struct CORO_THREAD_SCOPED_CAPABILITY callback_lock : async_coro::unique_lock<light_mutex> {
    using super = async_coro::unique_lock<light_mutex>;

    explicit callback_lock(await_callback_base& await) noexcept CORO_THREAD_EXCLUDES(await._continue_mutex)
        : super(await._continue_mutex, std::defer_lock) {}

    bool try_lock_mutex(await_callback_base& await) CORO_THREAD_TRY_ACQUIRE(true) {
      ASYNC_CORO_ASSERT(!this->owns_lock());

      while (!await._is_dying.load(std::memory_order::relaxed)) {
        if (this->try_lock()) {
          return true;
        }
      }

      return false;
    }
  };

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
    continue_callback& operator=(continue_callback&& other) = delete;

    ~continue_callback() noexcept {
      change_continue_no_lock(nullptr);
    }

    // void variant
    void operator()() const
      requires std::is_void_v<R>
    {
      unique_lock lock{_callback_mutex};

      if (_callback == nullptr) [[unlikely]] {
        return;
      }

      callback_lock continue_lock{*_callback};

      if (!continue_lock.try_lock_mutex(*_callback)) {
        return;
      }

      if (!_callback->_was_done.exchange(true, std::memory_order::relaxed)) {
        auto* clb = _callback;

        // no cancel here should happen. clb can't be destroyed at this point so we can release the lock
        continue_lock.unlock();
        lock.unlock();

        clb->_suspension.try_to_continue_from_any_thread(false);
        return;
      }
    }

    // non-void variant: accept a value to deliver to awaiting coroutine
    template <typename V>
      requires(!std::is_void_v<V> && std::is_constructible_v<R, V &&>)
    void operator()(V&& value) const {
      unique_lock lock{_callback_mutex};

      if (_callback == nullptr) [[unlikely]] {
        return;
      }

      callback_lock continue_lock{*_callback};

      if (!continue_lock.try_lock_mutex(*_callback)) {
        return;
      }

      if (!_callback->_was_done.exchange(true, std::memory_order::relaxed)) {
        auto* clb = _callback;

        // store result under the continue lock (we hold _continue_mutex via callback_lock)
        std::construct_at(std::addressof(clb->_result.res), std::forward<V>(value));
        clb->_has_result = true;

        // no cancel here should happen. clb can't be destroyed at this point so we can release the lock
        continue_lock.unlock();
        lock.unlock();

        clb->_suspension.try_to_continue_from_any_thread(false);
        return;
      }
    }

   private:
    bool change_continue_no_lock(continue_callback* new_ptr) noexcept CORO_THREAD_EXCLUDES(_callback_mutex, new_ptr->_callback_mutex, _callback->_continue_mutex) {
      unique_lock lock{_callback_mutex};

      if (new_ptr != nullptr) {
        unique_lock new_ptr_lock{new_ptr->_callback_mutex};

        if (_callback == nullptr) {
          new_ptr->_callback = nullptr;
          return false;
        }

        callback_lock continue_lock{*_callback};

        if (!continue_lock.try_lock_mutex(*_callback)) {
          new_ptr->_callback = nullptr;
          _callback = nullptr;
          return false;
        }

        new_ptr->_callback = _callback;
        _callback->_continue = new_ptr;
        _callback = nullptr;
      } else {
        if (_callback == nullptr) {
          return false;
        }

        callback_lock continue_lock{*_callback};
        if (!continue_lock.try_lock_mutex(*_callback)) {
          _callback = nullptr;
          return false;
        }

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
  using stored_result_t = std::conditional_t<std::is_void_v<R>, empty_result, non_initialised_result<R>>;

  coroutine_suspender _suspension;
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  continue_callback* _continue CORO_THREAD_GUARDED_BY(_continue_mutex) = nullptr;
  light_mutex _continue_mutex;
  stored_result_t _result;
  bool _has_result = false;
  std::atomic_bool _is_dying{false};
  std::atomic_bool _was_done{false};
};

template <typename T, typename R = void>
class await_callback : private await_callback_base<R> {
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
    this->_was_done.store(false, std::memory_order::relaxed);

    this->_suspension = handle.promise().suspend(2, &this->_on_cancel);

    this->_on_await(typename await_callback_base<R>::continue_callback{*this});

    this->_suspension.try_to_continue_immediately();
  }

  ASYNC_CORO_WARNINGS_MSVC_POP

  void await_resume() const noexcept
    requires std::is_void_v<R>
  {}

  auto await_resume() noexcept
    requires(!std::is_void_v<R>)
  {
    if constexpr (!std::is_reference_v<R>) {
      // return stored result (move)
      return std::move(this->_result.res);
    } else {
      // reference types: return reference to stored value
      return this->_result.res;
    }
  }

 private:
  T _on_await;
};

// deduction guide intentionally omitted: caller should specify R when needed

}  // namespace async_coro::internal
