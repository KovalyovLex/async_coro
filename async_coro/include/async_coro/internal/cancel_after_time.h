#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/scheduler.h>
#include <async_coro/utils/callback_on_stack.h>

#include <atomic>
#include <chrono>
#include <memory>

namespace async_coro::internal {

struct cancel_after_time : public advanced_awaiter<cancel_after_time> {
  using result_type = void;

  explicit cancel_after_time(std::chrono::steady_clock::duration sleep_duration) noexcept
      : _time(std::chrono::steady_clock::now() + sleep_duration) {}

  cancel_after_time(std::chrono::steady_clock::duration sleep_duration, execution_queue_mark execution_q) noexcept
      : _time(std::chrono::steady_clock::now() + sleep_duration),
        _execution_queue(execution_q) {}

  cancel_after_time(const cancel_after_time&) = delete;
  cancel_after_time(cancel_after_time&& other) noexcept
      : _time(other._time), _execution_queue(other._execution_queue) {
    ASYNC_CORO_ASSERT(other._handler == nullptr && "cancel_after_time should not be moved after awaiting");
  }

  ~cancel_after_time() noexcept = default;

  cancel_after_time& operator=(cancel_after_time&&) = delete;
  cancel_after_time& operator=(const cancel_after_time&) = delete;

  bool adv_await_ready() noexcept { return false; }  // NOLINT(*-static)

  void cancel_adv_await() {
    const auto tid = _t_id.exchange(delayed_task_id{}, std::memory_order::acquire);
    if (tid != delayed_task_id{}) {
      auto& system = _handler->get_scheduler().get_execution_system();

      if (system.cancel_execution(tid)) {
        // destroy continue function
        _continue_f = nullptr;
      }
      // else continue will be called later by system
    }
  }

  void adv_await_suspend(continue_callback_ptr continue_f, async_coro::base_handle& handle) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _continue_f = std::move(continue_f);
    _handler = std::addressof(handle);

    auto& system = handle.get_scheduler().get_execution_system();

    _t_id.store(system.plan_execution_after(
                    [this](const executor_data&) {
                      _t_id.exchange(delayed_task_id{}, std::memory_order::acquire);

                      auto ptr = _handler->get_owning_ptr();

                      // continue execution of our && or || combined task
                      _continue_f.execute_and_destroy(true);

                      // cancel our task
                      _handler->request_cancel();
                    },
                    _execution_queue, _time),
                std::memory_order::release);
  }

  void adv_await_resume() const noexcept {}

 private:
  void cancel_timer() noexcept {
  }

 private:
  continue_callback_ptr _continue_f = nullptr;
  async_coro::base_handle* _handler = nullptr;
  std::chrono::steady_clock::time_point _time;
  std::atomic<delayed_task_id> _t_id;
  execution_queue_mark _execution_queue = async_coro::execution_queues::any;
};

static_assert(std::atomic<delayed_task_id>::is_always_lock_free);

}  // namespace async_coro::internal
