#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/move_only_function.h>
#include <async_coro/task.h>
#include <async_coro/working_queue.h>

#include <coroutine>
#include <mutex>
#include <thread>
#include <vector>

namespace async_coro {
template <typename R>
struct task;

enum class execution_thread : std::uint8_t {
  undefined,
  main_thread,
  worker_thread
};

class scheduler {
 public:
  scheduler() noexcept;
  ~scheduler();

  // Executes planned tasks for main thread
  void update();

  template <typename R>
  task_handle<R> start_task(task<R> coro,
                            execution_thread thread = execution_thread::main_thread) {
    auto handle = coro.release_handle(internal::passkey{this});
    if (!handle.done()) [[likely]] {
      add_coroutine(handle.promise(), thread);
      return task_handle<R>{std::move(handle)};
    }
    return {};
  }

  working_queue& get_working_queue() noexcept { return _queue; }

  bool is_current_thread_fits(execution_thread thread) noexcept;

  // TODO: make it private
  // Embed coroutine
  void on_child_coro_added(base_handle& parent, base_handle& child);
  // Can continue execution immediatelly or do same as plan_continue_execution
  void continue_execution(base_handle& handle_impl);
  // Only plans excution on thread that was assigned for the coro. So continue will be called later
  void plan_continue_execution(base_handle& handle_impl) noexcept;
  // Always plan excution on thread and changes aggigned thread of coro.
  void change_thread(base_handle& handle_impl, execution_thread thread);

 private:
  void add_coroutine(base_handle& handle_impl, execution_thread thread);
  void continue_execution_impl(base_handle& handle_impl);
  void plan_continue_on_thread(base_handle& handle_impl, execution_thread thread);

 private:
  std::mutex _task_mutex;
  std::mutex _mutex;
  working_queue _queue;
  std::vector<base_handle*> _managed_coroutines;  // guarded by _mutex
  std::vector<move_only_function<void(scheduler&)>> _update_tasks;
  std::vector<move_only_function<void(scheduler&)>> _update_tasks_syncronized;  // guarded by _task_mutex
  std::thread::id _main_thread = {};
  bool _is_destroying = false;  // guarded by _mutex
  std::atomic_bool _has_syncronized_tasks = false;
};
}  // namespace async_coro
