#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/thread_safety/analysis.h>
#include <async_coro/internal/thread_safety/mutex.h>
#include <async_coro/task.h>
#include <async_coro/unique_function.h>
#include <async_coro/working_queue.h>

#include <thread>
#include <vector>

namespace async_coro {

template <typename R>
struct task;

class base_handle;

enum class execution_thread : std::uint8_t {
  undefined,
  main,
  worker
};

class scheduler {
 public:
  scheduler() noexcept;
  ~scheduler();

  // Executes planned tasks for main thread
  void update();

  // Schedules task and starts it execution
  template <typename R>
  task_handle<R> start_task(task<R> coro,
                            execution_thread thread = execution_thread::main) {
    auto handle = coro.release_handle(internal::passkey{this});
    task_handle<R> result{handle};
    if (!handle.done()) [[likely]] {
      add_coroutine(handle.promise(), thread);
      return result;
    }
    // free task as we released handle
    handle.promise().try_free_task_impl();
    return result;
  }

  // Returns working queue associated with this scheduler
  working_queue& get_working_queue() noexcept { return _queue; }

 public:
  // for internal api use

  // Checks is current thread fits requested 'thread'
  bool is_current_thread_fits(execution_thread thread) noexcept;
  // Embed coroutine
  void on_child_coro_added(base_handle& parent, base_handle& child);
  // Can continue execution immediately or do same as plan_continue_execution
  void continue_execution(base_handle& handle_impl);
  // Only plans execution on thread that was assigned for the coro. So continue will be called later
  void plan_continue_execution(base_handle& handle_impl) noexcept;
  // Always plan execution on thread and changes assigned thread of coro.
  void change_thread(base_handle& handle_impl, execution_thread thread);

 private:
  void add_coroutine(base_handle& handle_impl, execution_thread thread);
  void continue_execution_impl(base_handle& handle_impl);
  void plan_continue_on_thread(base_handle& handle_impl, execution_thread thread);

 private:
  mutex _task_mutex;
  mutex _mutex;
  working_queue _queue;
  std::vector<base_handle*> COTHREAD_GUARDED_BY(_mutex) _managed_coroutines;
  std::vector<unique_function<void(scheduler&)>> _update_tasks;
  std::vector<unique_function<void(scheduler&)>> COTHREAD_GUARDED_BY(_task_mutex) _update_tasks_synchronized;
  std::thread::id _main_thread = {};
  bool _is_destroying COTHREAD_GUARDED_BY(_mutex) = false;
  std::atomic_bool _has_synchronized_tasks = false;
};

}  // namespace async_coro
