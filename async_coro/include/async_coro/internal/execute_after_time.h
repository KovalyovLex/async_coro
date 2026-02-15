#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/advanced_awaiter_fwd.h>
#include <async_coro/scheduler.h>

#include <chrono>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <class Fx>
struct execute_after_time : public advanced_awaiter<execute_after_time<Fx>> {
  using result_type = decltype(std::declval<Fx>()());

  execute_after_time(Fx func, std::chrono::steady_clock::duration sleep_duration) noexcept(std::is_nothrow_move_constructible_v<Fx>)
      : _func(std::move(func)),
        _time(std::chrono::steady_clock::now() + sleep_duration) {}

  execute_after_time(const execute_after_time&) = delete;
  execute_after_time(execute_after_time&&) noexcept = default;

  ~execute_after_time() noexcept = default;

  execute_after_time& operator=(execute_after_time&&) = delete;
  execute_after_time& operator=(const execute_after_time&) = delete;

  [[nodiscard]] bool adv_await_ready() const noexcept { return false; }  // NOLINT(*-static)

  void cancel_adv_await() {
    cancel_timer();
  }

  void adv_await_suspend(continue_callback_ptr continue_f, async_coro::base_handle& handle) {
    _promise = std::addressof(handle);

    auto& execution_system = handle.get_scheduler().get_execution_system();

    _t_id = execution_system.plan_execution_after(
        [continue_f = std::move(continue_f)](const executor_data&) mutable {
          continue_f.execute_and_destroy(false);
        },
        handle.get_execution_queue(),
        _time);
  }

  result_type adv_await_resume() { return _func(); }

 private:
  void cancel_timer() noexcept {
    if (_t_id != delayed_task_id{}) {
      auto& execution_sys = _promise->get_scheduler().get_execution_system();

      execution_sys.cancel_execution(std::exchange(_t_id, delayed_task_id{}));
    }
  }

 private:
  base_handle* _promise = nullptr;
  Fx _func;
  std::chrono::steady_clock::time_point _time;
  delayed_task_id _t_id;
};

}  // namespace async_coro::internal
