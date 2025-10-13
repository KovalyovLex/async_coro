#pragma once

#include <async_coro/atomic_queue.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/hardware_interference_size.h>
#include <async_coro/thread_notifier.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/condition_variable.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/unique_function.h>
#include <async_coro/warnings.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace async_coro {

/**
 * @brief Configuration for a single execution thread
 *
 * This struct defines the configuration parameters for individual worker threads
 * in the execution system. It specifies the thread name and which execution queues
 * the thread is allowed to process.
 */
struct execution_thread_config {
  /**
   * @brief Constructs a thread configuration with a name
   * @param name The name of the thread (used for debugging and identification)
   */
  execution_thread_config(std::string name) : name(std::move(name)) {}  // NOLINT(*-explicit-*)

  /**
   * @brief Constructs a thread configuration with name and allowed task mask
   * @param name The name of the thread
   * @param m The execution thread mask defining which queues this thread can process
   */
  execution_thread_config(std::string name, execution_thread_mask mark) : name(std::move(name)), allowed_tasks(mark) {}

  /**
   * @brief Constructs a thread configuration with name and queue mark
   * @param name The name of the thread
   * @param m The execution queue mark defining which queues this thread can process
   */
  execution_thread_config(std::string name, execution_queue_mark mark) : name(std::move(name)), allowed_tasks(mark) {}

  // The name of the thread for debugging and identification purposes
  std::string name;

  // Bit mask defining which execution queues this thread is allowed to process
  execution_thread_mask allowed_tasks = execution_queues::worker | execution_queues::any;

  // Num empty worker loops to do before going to sleep on notifier
  std::size_t num_loops_before_sleep = 30;  // NOLINT(*-magic-*)
};

/**
 * @brief Configuration for the entire execution system
 *
 * This struct contains the complete configuration for an execution system instance,
 * including worker thread configurations and main thread task permissions.
 */
struct execution_system_config {
  // Vector of worker thread configurations defining all worker threads
  std::vector<execution_thread_config> worker_configs;

  // Bit mask defining which execution queues the main thread can process
  execution_thread_mask main_thread_allowed_tasks = execution_queues::main | execution_queues::any;
};

/**
 * @brief Multi-threaded execution system for asynchronous task processing
 *
 * The execution_system class provides a thread-safe, multi-threaded environment
 * for executing asynchronous tasks across different execution queues. It manages
 * worker threads, task distribution, and provides a unified interface for
 * scheduling and executing tasks.
 *
 * Key features:
 * - Multiple worker threads with configurable task permissions
 * - Thread-safe task queuing and execution
 * - Support for different execution queues (main, worker, any)
 * - Automatic task distribution based on thread capabilities
 * - Main thread integration for immediate task execution
 *
 * @note This class should only be created from the main thread that will call update_from_main()
 * @note The execution system automatically manages thread lifecycle and task distribution
 */
class execution_system : public i_execution_system {
 public:
  /**
   * @brief Constructs an execution system with the specified configuration
   *
   * Creates a new execution system instance with worker threads configured according
   * to the provided configuration. The number of worker threads will equal the number
   * of worker_configs in the configuration.
   *
   * @param config The configuration defining worker threads and main thread permissions
   * @param max_queue The maximum queue mark that this execution system can handle
   *
   * @note Should be created only from the "main" thread that will call update_from_main()
   * @note The execution system will start all worker threads immediately upon construction
   */
  explicit execution_system(const execution_system_config &config, execution_queue_mark max_queue = execution_queues::any);

  execution_system(const execution_system &) = delete;
  execution_system(execution_system &&) = delete;

  execution_system &operator=(const execution_system &) = delete;
  execution_system &operator=(execution_system &&) = delete;

  /**
   * @brief Destructor that properly shuts down the execution system
   *
   * Ensures all worker threads are properly stopped and joined before destruction.
   * This prevents resource leaks and ensures clean shutdown.
   */
  ~execution_system() noexcept override;

  /**
   * @brief Schedules a task for execution on the specified queue
   *
   * Adds the provided task function to the appropriate execution queue for
   * later execution by an available worker thread or the main thread.
   *
   * @param f The task function to be executed
   * @param execution_queue The execution queue where the task should be scheduled
   *
   * @note The task will be executed asynchronously by an appropriate thread
   * @note Thread safety: This method is thread-safe and can be called from any thread
   */
  void plan_execution(task_function func, execution_queue_mark execution_queue) override;

  /**
   * @brief Executes a task immediately if possible, otherwise schedules it
   *
   * Attempts to execute the task immediately on the current thread if it's
   * appropriate for the specified execution queue. If immediate execution
   * is not possible, the task is scheduled for later execution.
   *
   * @param f The task function to be executed
   * @param execution_queue The execution queue where the task should be executed
   *
   * @note This method provides better performance for tasks that can be executed immediately
   * @note Thread safety: This method is thread-safe and can be called from any thread
   */
  void execute_or_plan_execution(task_function func, execution_queue_mark execution_queue) override;

  /**
   * @brief Schedules a task to be executed at or after the given steady_clock time
   *
   * The task will be queued into the requested execution queue when the time
   * point is reached.
   *
   * @note Thread safety: This method is thread-safe and can be called from any thread
   */
  delayed_task_id plan_execution_after(task_function func, execution_queue_mark execution_queue,
                                       std::chrono::steady_clock::time_point when) override;

  /**
   * @brief Cancels execution of previously scheduled function
   *
   * @param task_id delayed_task_id structure returned from 'plan_execution_after'
   * @return true if task was cancelled false otherwise
   *
   * @note Thread safety: This method is thread-safe and can be called from any thread
   */
  bool cancel_execution(const delayed_task_id &task_id) override;

  /**
   * @brief Checks if the current thread can execute tasks from the specified queue
   *
   * Determines whether the calling thread has permission to execute tasks
   * from the given execution queue based on its configuration.
   *
   * @param execution_queue The execution queue to check against
   * @return true if the current thread can execute tasks from the specified queue, false otherwise
   *
   * @note This method is useful for determining if immediate execution is possible
   */
  [[nodiscard]] bool is_current_thread_fits(execution_queue_mark execution_queue) const noexcept override;

  /**
   * @brief Processes one task from the main thread's execution queues
   *
   * Attempts to execute one pending task that is appropriate for the main thread.
   * This method should be called periodically from the main thread to process
   * tasks that are specifically designated for main thread execution.
   *
   * @note This method should only be called from the main thread
   * @note If no tasks are available, this method returns immediately
   * @note This method is non-blocking and designed for integration with main thread event loops
   */
  void update_from_main();

  /**
   * @brief Returns the current number of worker threads
   *
   * @return The number of worker threads currently managed by this execution system
   *
   * @note This count does not include the main thread
   */
  [[nodiscard]] std::uint32_t get_num_worker_threads() const noexcept { return _num_workers; }

  /**
   * @brief Returns the number of workers that can process tasks from the specified queue
   *
   * Counts the total number of threads (including the main thread) that have
   * permission to execute tasks from the given execution queue.
   *
   * @param execution_queue The execution queue to count workers for
   * @return The number of threads that can process tasks from the specified queue
   *
   * @note This includes both worker threads and the main thread if it has appropriate permissions
   */
  [[nodiscard]] std::uint32_t get_num_workers_for_queue(execution_queue_mark execution_queue) const noexcept;

 private:
  struct worker_thread_data;

  /**
   * @brief Main loop for worker threads
   *
   * This is the core execution loop that runs in each worker thread.
   * It continuously processes tasks from the thread's assigned queues
   * until the execution system is stopped.
   *
   * @param data Reference to the worker thread's data structure
   *
   * @note This method runs in a separate thread for each worker
   * @note The loop will exit when the execution system is being shut down
   */
  void worker_loop(worker_thread_data &data);

  /**
   * @brief loop for delayed tasks
   */
  void timer_loop();

  /**
   * @brief Sets the name of a thread for debugging purposes
   *
   * Attempts to set the thread name using platform-specific APIs.
   * This is useful for debugging and profiling tools.
   *
   * @param thread Reference to the thread whose name should be set
   * @param name The name to assign to the thread
   *
   * @note This is a platform-dependent operation and may not work on all systems
   */
  static void set_thread_name(std::thread &thread, const std::string &name);

 private:
  using t_task_id = decltype(std::declval<delayed_task_id>().task_id);

  // ...internal delayed task handling
  class delayed_task {
   public:
    task_function func;
    t_task_id id;
    std::chrono::steady_clock::time_point when;
    execution_queue_mark queue;
    bool cancel_execution = false;

    delayed_task(task_function &&task,
                 std::chrono::steady_clock::time_point when_tp,
                 execution_queue_mark queue_mark,
                 t_task_id t_id) noexcept
        : func(std::move(task)),
          id(t_id),
          when(when_tp),
          queue(queue_mark) {}

    auto operator<=>(const delayed_task &other) const noexcept { return when <=> other.when; }
  };

  // Type alias for the task queue using atomic_queue
  using tasks = atomic_queue<task_function>;

  ASYNC_CORO_WARNINGS_MSVC_PUSH
  ASYNC_CORO_WARNINGS_MSVC_IGNORE(4324)

  /**
   * @brief Data structure containing information for each worker thread
   *
   * This struct holds all the per-thread data needed to manage worker threads,
   * including the thread object, assigned task queues, permissions, and notification mechanism.
   */
  struct alignas(std::hardware_constructive_interference_size) worker_thread_data {
    // Notification mechanism for waking up the worker thread
    thread_notifier notifier;

    // The actual thread object
    std::thread thread;

    // Pointers to task queues this worker can process
    std::vector<tasks *> task_queues;

    // Bit mask defining which execution queues this worker can process
    execution_thread_mask mask;

    // Num empty worker loops to do before going to sleep on notifier
    std::size_t num_loops_before_sleep = 0;
  };

  ASYNC_CORO_WARNINGS_MSVC_POP

  /**
   * @brief Data structure for managing a single execution queue
   *
   * This struct represents a single execution queue and contains both the
   * queue itself and references to all worker threads that can process it.
   */
  struct task_queue {
    // The actual task queue containing pending tasks
    tasks queue;

    // Pointers to worker threads that can execute tasks from this queue
    std::vector<worker_thread_data *> workers_data;
  };

  // Array of task queues, one for each execution queue mark
  // NOLINTNEXTLINE(*-avoid-c-arrays)
  std::unique_ptr<task_queue[]> _tasks_queues;

  // Pointers to task queues that the main thread can process
  std::vector<tasks *> _main_thread_queues;

  // Array of worker thread data structures
  // NOLINTNEXTLINE(*-avoid-c-arrays)
  std::unique_ptr<worker_thread_data[]> _thread_data;

  // ID of the main thread for identification purposes
  std::thread::id _main_thread_id;

  // Bit mask defining which execution queues the main thread can process
  const execution_thread_mask _main_thread_mask;

  // Number of worker threads in the system
  const std::uint32_t _num_workers;

  // Maximum execution queue mark that this system can handle
  const execution_queue_mark _max_q;

  // Atomic flag indicating whether the system is in the process of shutting down
  std::atomic_bool _is_stopping{false};

  // Delayed tasks
  async_coro::mutex _delayed_mutex;
  async_coro::condition_variable _delayed_cv;

  std::vector<delayed_task> _delayed_tasks CORO_THREAD_GUARDED_BY(_delayed_mutex);
  std::thread _timer_thread;
  t_task_id _delayed_task_id = 1;
};

}  // namespace async_coro
