#pragma once

#include <async_coro/config.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/task_handle.h>
#include <async_coro/task_launcher.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>

#include <utility>
#include <vector>

namespace async_coro {

class base_handle;

/**
 * @class scheduler
 * @brief Manages coroutine execution and scheduling.
 *
 * The scheduler is responsible for starting tasks, continuing their execution,
 * and managing the underlying execution system (e.g., thread pools).
 * Each scheduler has its own execution system.
 */
class scheduler {
 public:
  /**
   * @brief Constructs a scheduler with a default execution system.
   */
  scheduler();

  /**
   * @brief Constructs a scheduler with a provided execution system.
   * @param system The execution system to use for scheduling tasks.
   */
  explicit scheduler(i_execution_system::ptr system) noexcept;

  /**
   * @brief Destroys the scheduler, destroying all managed coroutines.
   */
  ~scheduler();

  /**
   * @brief Schedules a task and starts its execution.
   *
   * This function takes a coroutine task, assigns it to an execution queue,
   * and begins its execution. If the task is already completed, it is freed immediately.
   *
   * @tparam R The return type of the task.
   * @param coro The task to be executed.
   * @param execution_queue The execution queue to run the task on. Defaults to the main queue.
   * @return A handle to the started task.
   */
  template <typename R>
  task_handle<R> start_task(task_launcher<R> launcher) {
    auto coro = launcher.launch();
    auto handle = coro.release_handle(internal::passkey{this});
    task_handle<R> result{handle};
    if (!handle.done()) [[likely]] {
      add_coroutine(handle.promise(), launcher.get_start_function(), launcher.get_execution_queue());
      return result;
    }
    // free task as we released handle
    handle.promise().on_task_freed_by_scheduler();
    return result;
  }

  /**
   * @brief Schedules a coroutine or function for execution.
   * @tparam T The type of the coroutine or function.
   * @param coroutine_or_function The coroutine or function to be executed.
   * @return A handle to the started task.
   */
  template <typename T>
  auto start_task(T&& coroutine_or_function, execution_queue_mark execution_queue = execution_queues::main) {
    return start_task(task_launcher{std::forward<T>(coroutine_or_function), execution_queue});
  }

  /**
   * @brief Gets a reference to the execution system.
   * @tparam T The type of the execution system, must derive from i_execution_system.
   * @return A reference to the execution system.
   */
  template <class T>
    requires(std::derived_from<T, i_execution_system>)
  T& get_execution_system() const noexcept {
    ASYNC_CORO_ASSERT(dynamic_cast<T*>(_execution_system.get()));

    return *static_cast<T*>(_execution_system.get());
  }

 public:
  // for internal api use

  /**
   * @brief Continues the execution of a coroutine.
   * @details For internal use. This will either execute the coroutine immediately if the current
   * thread is suitable, or schedule it for later execution.
   * @param handle_impl The handle of the coroutine to continue.
   */
  void continue_execution(base_handle& handle_impl);

  /**
   * @brief Schedules a coroutine for continued execution on its assigned thread.
   * @details For internal use. Unlike `continue_execution`, this method always schedules
   * the coroutine for later execution without attempting to run it immediately.
   * @param handle_impl The handle of the coroutine to schedule.
   */
  void plan_continue_execution(base_handle& handle_impl);

  /**
   * @brief Moves a coroutine to a different execution queue.
   * @details For internal use. The coroutine's next execution will be on the new queue.
   * @param handle_impl The handle of the coroutine to move.
   * @param execution_queue The target execution queue.
   */
  void change_execution_queue(base_handle& handle_impl, execution_queue_mark execution_queue);

  // Embed coroutine
  void on_child_coro_added(base_handle& parent, base_handle& child, internal::passkey<task_base>);

 private:
  bool is_current_thread_fits(execution_queue_mark execution_queue) noexcept;
  void add_coroutine(base_handle& handle_impl, callback_base::ptr start_function, execution_queue_mark execution_queue);
  void continue_execution_impl(base_handle& handle_impl);
  void plan_continue_on_thread(base_handle& handle_impl, execution_queue_mark execution_queue);

 private:
  mutex _mutex;
  i_execution_system::ptr _execution_system;
  std::vector<base_handle*> CORO_THREAD_GUARDED_BY(_mutex) _managed_coroutines;
  bool _is_destroying CORO_THREAD_GUARDED_BY(_mutex) = false;
};

}  // namespace async_coro
