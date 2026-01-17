#pragma once

#include <async_coro/config.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/task_handle.h>
#include <async_coro/task_launcher.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/utils/passkey.h>

#if ASYNC_CORO_WITH_EXCEPTIONS
#include <exception>
#endif
#include <utility>
#include <vector>

namespace async_coro {

class base_handle;
namespace internal {
class coroutine_suspender;
}

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

  scheduler(const scheduler&) = delete;
  scheduler(scheduler&&) = delete;

  /**
   * @brief Constructs a scheduler with a provided execution system.
   * @param system The execution system to use for scheduling tasks.
   */
  explicit scheduler(i_execution_system::ptr system) noexcept;

  /**
   * @brief Destroys the scheduler, destroying all managed coroutines.
   */
  ~scheduler();

  scheduler& operator=(const scheduler&) = delete;
  scheduler& operator=(scheduler&&) = delete;

  /**
   * @brief Schedules a task and starts its execution.
   *
   * This function takes a coroutine task, assigns it to an execution queue,
   * and begins its execution. If the task is already completed, it is freed immediately.
   *
   * @tparam R The return type of the task.
   * @param launcher The task wrapped to task_launcher to be executed..
   * @return A handle to the started task.
   */
  template <typename R>
  task_handle<R> start_task(task_launcher<R> launcher) {
    auto coro = launcher.launch();
    auto handle = coro.release_handle(passkey{this});
    task_handle<R> result{handle, transfer_ownership{}};
    if (!handle.done()) [[likely]] {
      add_coroutine(handle.promise(), launcher.get_start_function(), launcher.get_execution_queue());
      return result;
    }
    handle.promise().check_exception();
    return result;
  }

  /**
   * @brief Schedules a coroutine or function for execution.
   * @tparam T The type of the coroutine or function.
   * @param coroutine_or_function The coroutine or function to be executed.
   * @return A handle to the started task.
   */
  template <typename... RArgs>
    requires(is_task_launchable<RArgs...>)
  auto start_task(RArgs&&... launcher_args) {
    return start_task(task_launcher{std::forward<RArgs>(launcher_args)...});
  }

  /**
   * @brief Gets a reference to the execution system.
   * @tparam T The type of the execution system, must derive from i_execution_system.
   * @return A reference to the execution system.
   */
  template <class T>
    requires(std::derived_from<T, i_execution_system>)
  [[nodiscard]] T& get_execution_system() const noexcept {
    ASYNC_CORO_ASSERT(dynamic_cast<T*>(_execution_system.get()));

    return *static_cast<T*>(_execution_system.get());
  }

  /**
   * @brief Gets a reference to the execution system.
   * @return A reference to the execution system.
   */
  [[nodiscard]] i_execution_system& get_execution_system() const noexcept {
    return *_execution_system.get();
  }

#if ASYNC_CORO_WITH_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  void set_unhandled_exception_handler(unique_function<void(std::exception_ptr)> handler) noexcept;
#endif

 public:
  // for internal api use

  /**
   * @brief Continues the execution of a coroutine.
   * @details For internal use. This will either execute the coroutine immediately if the current
   * thread is suitable, or schedule it for later execution.
   * @param handle_impl The handle of the coroutine to continue.
   */
  void continue_execution(base_handle& handle_impl, passkey_any<internal::coroutine_suspender, base_handle>);

  /**
   * @brief Embed coroutine. Parent and child coroutines switch to suspended state, child will be continued after parent suspension point
   * @param parent The handle of the owning coroutine.
   * @param parent The handle of the coroutine to embed into parent.
   */
  void on_child_coro_added(base_handle& parent, base_handle& child, passkey<task_base>);

 private:
  bool is_current_thread_fits(execution_queue_mark execution_queue) noexcept;
  void add_coroutine(base_handle& handle_impl, callback_base::ptr start_function, execution_queue_mark execution_queue);
  void continue_execution_impl(base_handle& handle_impl);
  void plan_continue_on_thread(base_handle& handle_impl, execution_queue_mark execution_queue);
  void change_execution_queue(base_handle& handle_impl, execution_queue_mark execution_queue);
  void cleanup_coroutine(base_handle& handle_impl, bool cancelled);

 private:
  mutex _mutex;
  i_execution_system::ptr _execution_system;
  std::vector<base_handle_ptr> CORO_THREAD_GUARDED_BY(_mutex) _managed_coroutines;
  bool _is_destroying CORO_THREAD_GUARDED_BY(_mutex) = false;

#if ASYNC_CORO_WITH_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  std::shared_ptr<unique_function<void(std::exception_ptr)>> _exception_handler CORO_THREAD_GUARDED_BY(_mutex);
#endif
};

}  // namespace async_coro
