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
/**
 * @brief Awaitable that suspends a coroutine for a duration or until a steady_clock timepoint
 *
 * This awaitable is intended to be used via the public helpers in
 * `async_coro::sleep(...)` and is designed to be awaited inside
 * coroutines (i.e. `co_await async_coro::sleep(...)`).
 *
 * Behavior summary:
 * - When awaited, the coroutine is suspended and a delayed timer is scheduled
 *   in the execution system associated with the coroutine's scheduler.
 * - When the timer expires (or the task is cancelled), the coroutine is
 *   resumed via the scheduler's continuation mechanism.
 * - The awaitable supports specifying an explicit `execution_queue_mark` to
 *   control which execution queue will resume the coroutine. If not provided,
 *   the awaitable will use the parent's execution queue (see `coro_await_transform`).
 * - Cancellation is supported: if the awaitable is destroyed before the timer
 *   fires, any scheduled timer is cancelled and the coroutine is resumed
 *   immediately via the promise's `continue_after_sleep()` path.
 *
 * Thread-safety and locking:
 * - Internal synchronization is provided by a lightweight mutex (`light_mutex`)
 *   to protect callback state. The awaitable carefully acquires and releases
 *   locks when interacting with the execution system and the callback that
 *   performs the resume.
 *
 * Example usage:
 * @code
 * co_await async_coro::sleep(std::chrono::milliseconds{100});
 * co_await async_coro::sleep(std::chrono::seconds{1}, execution_queues::worker);
 * @endcode
 */
struct await_sleep {
  explicit await_sleep(std::chrono::steady_clock::duration sleep_duration) noexcept
      : _on_cancel(this),
        _time(std::chrono::steady_clock::now() + sleep_duration),
        _use_parent_q(true) {}
  await_sleep(std::chrono::steady_clock::duration sleep_duration, execution_queue_mark execution_q) noexcept
      : _on_cancel(this),
        _time(std::chrono::steady_clock::now() + sleep_duration),
        _execution_queue(execution_q),
        _use_parent_q(false) {}

  await_sleep(const await_sleep&) = delete;
  await_sleep(await_sleep&&) = delete;

  ~await_sleep() noexcept = default;

  await_sleep& operator=(await_sleep&&) = delete;
  await_sleep& operator=(const await_sleep&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return false; }  // NOLINT(*-static)

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> handle) {
    _promise = std::addressof(handle.promise());

    // self destroy protection
    auto ptr = _promise->get_owning_ptr();

    // Register cancellation callback with the promise so that if the parent
    // cancels the operation the timer can be cancelled as well.
    _promise->plan_sleep_on_queue(_execution_queue, _on_cancel);

    auto& execution_system = _promise->get_scheduler().get_execution_system();

    _t_id.store(execution_system.plan_execution_after(
                    [ptr = std::move(ptr)] {
                      ptr->continue_after_sleep();
                    },
                    _execution_queue, _time),
                std::memory_order::relaxed);
  }

  void await_resume() const noexcept {}

  await_sleep& coro_await_transform(base_handle& parent) noexcept {
    if (_use_parent_q) {
      _execution_queue = parent.get_execution_queue();
    }

    return *this;
  }

 private:
  struct on_cancel_callback {
    void operator()() const {
      auto tid = clb->_t_id.exchange(delayed_task_id{}, std::memory_order::relaxed);
      if (tid != delayed_task_id{}) {
        auto& execution_sys = clb->_promise->get_scheduler().get_execution_system();

        if (execution_sys.cancel_execution(tid)) {
          // continue execution to run cancel logic
          clb->_promise->continue_after_sleep();
        }
        // else continue will be called later by system
      }
    }

    await_sleep* clb;
  };

 private:
  base_handle* _promise = nullptr;
  callback_on_stack<on_cancel_callback, void()> _on_cancel;
  std::chrono::steady_clock::time_point _time;
  std::atomic<delayed_task_id> _t_id;
  execution_queue_mark _execution_queue = async_coro::execution_queues::any;
  bool _use_parent_q;
};

static_assert(std::atomic<delayed_task_id>::is_always_lock_free);

}  // namespace async_coro::internal
