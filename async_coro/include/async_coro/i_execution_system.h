#pragma once

#include <async_coro/execution_queue_mark.h>
#include <async_coro/utils/unique_function.h>

#include <chrono>
#include <memory>

namespace async_coro {

struct delayed_task_id {
  size_t task_id{};

  auto operator<=>(const delayed_task_id &) const noexcept = default;
};

/**
 * @brief Abstract interface for execution systems
 *
 * The i_execution_system interface defines the contract for execution systems
 * that can schedule and execute asynchronous tasks across different execution queues.
 * This interface provides a unified abstraction for task execution regardless of
 * the underlying implementation (single-threaded, multi-threaded, etc.).
 *
 * Key responsibilities:
 * - Task scheduling and execution management
 * - Execution queue routing and prioritization
 * - Thread capability checking and task distribution
 * - Support for immediate execution when possible
 *
 * Implementations of this interface can provide different execution strategies:
 * - Multi-threaded execution with worker pools
 * - Single-threaded execution for testing or simple scenarios
 * - Custom execution strategies for specific use cases
 *
 * @note All methods in this interface are thread-safe unless otherwise specified
 * @note Implementations should ensure proper resource management and thread safety
 */
class i_execution_system {
 public:
  /**
   * @brief Type alias for task functions with small function optimization
   *
   * Defines the signature for tasks that can be executed by the execution system.
   * Uses unique_function with a small function optimization buffer of 3 pointers
   * to avoid heap allocations for small function objects.
   *
   * Task functions should be callable objects (functions, lambdas, function objects)
   * that take no parameters and return void.
   */
  using task_function = unique_function<void(), sizeof(void *) * 3>;

  /**
   * @brief Type alias for unique pointer to execution system interface
   *
   * Provides a convenient way to manage execution system instances through
   * smart pointers, ensuring proper resource management and ownership semantics.
   */
  using ptr = std::unique_ptr<i_execution_system>;

  i_execution_system() noexcept = default;
  i_execution_system(const i_execution_system &) noexcept = default;
  i_execution_system(i_execution_system &&) noexcept = default;

  virtual ~i_execution_system() noexcept = default;

  i_execution_system &operator=(const i_execution_system &) noexcept = default;
  i_execution_system &operator=(i_execution_system &&) noexcept = default;

  /**
   * @brief Schedules a task for execution on the specified queue
   *
   * Adds the provided task function to the appropriate execution queue for
   * later execution. The task will be executed by an appropriate thread
   * based on the execution queue configuration and thread capabilities.
   *
   * This method is the primary interface for asynchronous task scheduling.
   * Tasks are queued and executed asynchronously, allowing the calling thread
   * to continue execution immediately.
   *
   * @param f The task function to be executed. Must be a callable object
   *          that takes no parameters and returns void.
   * @param execution_queue The execution queue marker indicating which queue
   *                       should process this task (e.g., main, worker, any)
   *
   * @note This method is thread-safe and can be called from any thread
   * @note The task will be executed asynchronously by an appropriate thread
   * @note Task execution order within a queue is not guaranteed unless specified by the implementation
   * @note The task function object will be moved into the execution system
   */
  virtual void plan_execution(task_function func, execution_queue_mark execution_queue) = 0;

  /**
   * @brief Schedules a task for execution on the specified queue at the given time
   *
   * The task will be executed no earlier than the provided steady_clock time point.
   *
   * @param func The task function to be executed
   * @param execution_queue The execution queue where the task should be scheduled
   * @param when The steady_clock time point when the task should be executed
   */
  virtual delayed_task_id plan_execution_after(task_function func, execution_queue_mark execution_queue,
                                               std::chrono::steady_clock::time_point when) = 0;

  /**
   * @brief Cancels execution of previously scheduled function
   *
   * @param task_id delayed_task_id structure returned from 'plan_execution_after'
   * @return true if task was cancelled false otherwise
   *
   * @note If this method returns false execution may still happen if task was already scheduled for asap execution on the queue
   */
  virtual bool cancel_execution(const delayed_task_id &task_id) = 0;

  /**
   * @brief Executes a task immediately if possible, otherwise schedules it
   *
   * Attempts to execute the task immediately on the current thread if it's
   * appropriate for the specified execution queue. If immediate execution
   * is not possible (e.g., current thread doesn't have permission for the queue),
   * the task is scheduled for later execution using plan_execution().
   *
   * This method provides better performance for tasks that can be executed
   * immediately, avoiding the overhead of task queuing and thread switching.
   *
   * @param f The task function to be executed. Must be a callable object
   *          that takes no parameters and returns void.
   * @param execution_queue The execution queue marker indicating which queue
   *                       should process this task
   *
   * @note This method is thread-safe and can be called from any thread
   * @note Immediate execution occurs if the current thread is a worker or main thread
   *       and the execution_queue fits the thread's configuration masks
   * @note If immediate execution is not possible, the task is queued for later execution
   * @note This method provides better performance than plan_execution() when immediate
   *       execution is possible
   */
  virtual void execute_or_plan_execution(task_function func, execution_queue_mark execution_queue) = 0;

  /**
   * @brief Checks if the current thread can execute tasks from the specified queue
   *
   * Determines whether the calling thread has permission to execute tasks
   * from the given execution queue based on the execution system's configuration.
   * This method is useful for determining if immediate execution is possible
   * before calling execute_or_plan_execution().
   *
   * @param execution_queue The execution queue marker to check against
   * @return true if the current thread is a worker or main thread and the
   *         execution_queue fits the system's configuration for that thread,
   *         false otherwise
   *
   * @note This method is thread-safe and can be called from any thread
   * @note This method is noexcept and will not throw exceptions
   * @note The result may change if the execution system configuration is modified
   * @note This method is useful for optimizing task execution by avoiding
   *       unnecessary queuing when immediate execution is possible
   */
  [[nodiscard]] virtual bool is_current_thread_fits(execution_queue_mark execution_queue) const noexcept = 0;
};

}  // namespace async_coro
