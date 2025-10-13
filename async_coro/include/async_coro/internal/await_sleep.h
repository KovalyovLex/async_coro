#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/light_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <atomic>
#include <concepts>
#include <memory>
#include <utility>

namespace async_coro::internal {

struct await_sleep {
  explicit await_sleep(std::chrono::steady_clock::duration sleep_duration) noexcept
      : _on_cancel(this),
        _time(std::chrono::steady_clock::now() + sleep_duration),
        _use_parent_q(true) {}
  await_sleep(std::chrono::steady_clock::duration sleep_duration, execution_queue_mark execution_q) noexcept
      : _on_cancel(this),
        _execution_queue(execution_q),
        _time(std::chrono::steady_clock::now() + sleep_duration),
        _use_parent_q(false) {}

  await_sleep(const await_sleep&) = delete;
  await_sleep(await_sleep&&) = delete;

  ~await_sleep() noexcept {
    reset_callback();
  }

  await_sleep& operator=(await_sleep&&) = delete;
  await_sleep& operator=(const await_sleep&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return false; }  // NOLINT(*-static)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    _promise = std::addressof(handle.promise());

    _promise->plan_sleep_on_queue(_execution_queue, _on_cancel);

    auto& execution_system = _promise->get_scheduler().get_execution_system();

    async_coro::unique_lock lock{_callback_mutex};
    _t_id = execution_system.plan_execution_after(
        execution_callback{this},
        _execution_queue, _time);
  }

  void await_resume() const noexcept {}

  await_sleep& coro_await_transform(base_handle& parent) noexcept {
    if (_use_parent_q) {
      _execution_queue = parent.get_execution_queue();
    }

    return *this;
  }

 private:
  void reset_callback() CORO_THREAD_EXCLUDES(_callback_mutex) {
    _is_dying.store(true, std::memory_order::release);

    async_coro::unique_lock lock_callback{_callback_mutex};

    if (_callback != nullptr) {
      async_coro::unique_lock lock{_callback->await_mutex};
      _callback->awaiter = nullptr;
      _callback = nullptr;
    }
  }

  void reset_callback_no_lock() CORO_THREAD_REQUIRES(_callback_mutex) {
    _is_dying.store(true, std::memory_order::release);

    if (_callback != nullptr) {
      async_coro::unique_lock lock{_callback->await_mutex};
      _callback->awaiter = nullptr;
      _callback = nullptr;
    }
  }

  struct CORO_THREAD_SCOPED_CAPABILITY callback_lock : async_coro::unique_lock<light_mutex> {
    using super = async_coro::unique_lock<light_mutex>;

    explicit callback_lock(await_sleep& await) noexcept CORO_THREAD_EXCLUDES(await._callback_mutex)
        : super(await._callback_mutex, std::defer_lock) {}

    bool try_lock_mutex(await_sleep& await) CORO_THREAD_TRY_ACQUIRE(true) {
      ASYNC_CORO_ASSERT(!this->owns_lock());

      while (!await._is_dying.load(std::memory_order::relaxed)) {
        if (this->try_lock()) {
          return true;
        }
      }

      return false;
    }
  };

  struct execution_callback {
    explicit execution_callback(await_sleep* awt) noexcept CORO_THREAD_REQUIRES(awt->_callback_mutex)
        : awaiter(awt) {
      ASYNC_CORO_ASSERT(awt->_callback == nullptr);

      awt->_callback = this;
    }
    execution_callback(const execution_callback&) = delete;
    execution_callback(execution_callback&& other) noexcept CORO_THREAD_EXCLUDES(other.await_mutex, other.awaiter->_callback_mutex) {
      async_coro::unique_lock lock{other.await_mutex};

      awaiter = std::exchange(other.awaiter, nullptr);
      if (awaiter != nullptr) {
        callback_lock lock_callback{*awaiter};
        if (lock_callback.try_lock_mutex(*awaiter)) {
          awaiter->_callback = this;
        } else {
          awaiter = nullptr;
        }
      }
    }
    ~execution_callback() noexcept CORO_THREAD_EXCLUDES(await_mutex, awaiter->_callback_mutex) {
      async_coro::unique_lock lock{await_mutex};

      if (awaiter != nullptr) {
        callback_lock lock_callback{*awaiter};
        if (lock_callback.try_lock_mutex(*awaiter)) {
          awaiter->_callback = nullptr;
        }
      }
    }
    execution_callback& operator=(const execution_callback&) = delete;
    execution_callback& operator=(execution_callback&& other) noexcept CORO_THREAD_EXCLUDES(await_mutex, other.await_mutex, awaiter->_callback_mutex) {
      if (this == &other) {
        return *this;
      }

      async_coro::unique_lock lock{await_mutex};

      if (awaiter != nullptr) {
        callback_lock lock_callback{*awaiter};
        if (lock_callback.try_lock_mutex(*awaiter)) {
          awaiter->_callback = nullptr;
        }
      }

      {
        async_coro::unique_lock lock2{other.await_mutex};
        awaiter = std::exchange(other.awaiter, nullptr);
      }

      if (awaiter != nullptr) {
        callback_lock lock_callback{*awaiter};
        if (lock_callback.try_lock_mutex(*awaiter)) {
          awaiter->_callback = this;
        } else {
          awaiter = nullptr;
        }
      }

      return *this;
    }

    void operator()() const CORO_THREAD_EXCLUDES(await_mutex, awaiter->_callback_mutex) {
      async_coro::unique_lock lock{await_mutex};

      if (awaiter == nullptr) {
        return;
      }

      callback_lock lock_callback{*awaiter};
      if (lock_callback.try_lock_mutex(*awaiter)) {
        awaiter->_callback = nullptr;
        awaiter->_t_id = {};
      } else {
        awaiter = nullptr;
        return;
      }

      if (!awaiter->_was_done.exchange(true, std::memory_order::relaxed)) {
        auto* promise = awaiter->_promise;
        awaiter = nullptr;
        lock_callback.unlock();
        lock.unlock();

        // resume
        promise->continue_after_sleep();
        return;
      }

      awaiter = nullptr;
    }

    mutable await_sleep* awaiter CORO_THREAD_GUARDED_BY(await_mutex);
    mutable light_mutex await_mutex;
  };

  struct on_cancel_callback {
    void operator()() const CORO_THREAD_EXCLUDES(clb->_callback_mutex) {
      if (!clb->_was_done.exchange(true, std::memory_order::relaxed)) {
        async_coro::unique_lock lock{clb->_callback_mutex};

        clb->reset_callback_no_lock();

        auto& execution_sys = clb->_promise->get_scheduler().get_execution_system();
        execution_sys.cancel_execution(clb->_t_id);
        lock.unlock();

        // continue execution
        clb->_promise->continue_after_sleep();
      }
    }

    await_sleep* clb;
  };

 private:
  light_mutex _callback_mutex;
  execution_callback* _callback CORO_THREAD_GUARDED_BY(_callback_mutex) = {nullptr};
  base_handle* _promise = nullptr;
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  execution_queue_mark _execution_queue = async_coro::execution_queues::any;
  std::chrono::steady_clock::time_point _time;
  delayed_task_id _t_id CORO_THREAD_GUARDED_BY(_callback_mutex) = {};
  bool _use_parent_q;
  std::atomic_bool _was_done{false};
  std::atomic_bool _is_dying{false};
};

}  // namespace async_coro::internal
