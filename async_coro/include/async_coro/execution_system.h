#pragma once

#include <async_coro/atomic_queue.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/thread_notifier.h>
#include <async_coro/unique_function.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
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
  execution_thread_config(std::string name) : name(std::move(name)) {}

  /**
   * @brief Constructs a thread configuration with name and allowed task mask
   * @param name The name of the thread
   * @param m The execution thread mask defining which queues this thread can process
   */
  execution_thread_config(std::string name, execution_thread_mask m) : name(std::move(name)), allowed_tasks(m) {}

  /**
   * @brief Constructs a thread configuration with name and queue mark
   * @param name The name of the thread
   * @param m The execution queue mark defining which queues this thread can process
   */
  execution_thread_config(std::string name, execution_queue_mark m) : name(std::move(name)), allowed_tasks(m) {}

  /** @brief The name of the thread for debugging and identification purposes */
  std::string name;

  /** @brief Bit mask defining which execution queues this thread is allowed to process */
  execution_thread_mask allowed_tasks = execution_queues::worker | execution_queues::any;

  /** @brief Num empty worker loops to do before going to sleep on notifier */
  std::size_t num_loops_before_sleep = 30;
};

/**
 * @brief Configuration for the entire execution system
 *
 * This struct contains the complete configuration for an execution system instance,
 * including worker thread configurations and main thread task permissions.
 */
struct execution_system_config {
  /** @brief Vector of worker thread configurations defining all worker threads */
  std::vector<execution_thread_config> worker_configs;

  /** @brief Bit mask defining which execution queues the main thread can process */
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
  execution_system(const execution_system_config &config, execution_queue_mark max_queue = execution_queues::any);

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
  void plan_execution(task_function f, execution_queue_mark execution_queue) override;

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
  void execute_or_plan_execution(task_function f, execution_queue_mark execution_queue) override;

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
  bool is_current_thread_fits(execution_queue_mark execution_queue) const noexcept override;

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
  uint32_t get_num_worker_threads() const noexcept { return _num_workers; }

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
  uint32_t get_num_workers_for_queue(execution_queue_mark execution_queue) const noexcept;

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
  /** @brief Type alias for the task queue using atomic_queue */
  using tasks = atomic_queue<task_function>;

  /**
   * @brief Data structure containing information for each worker thread
   *
   * This struct holds all the per-thread data needed to manage worker threads,
   * including the thread object, assigned task queues, permissions, and notification mechanism.
   */
  struct alignas(std::hardware_constructive_interference_size) worker_thread_data {
    /** @brief Notification mechanism for waking up the worker thread */
    thread_notifier notifier;

    /** @brief The actual thread object */
    std::thread thread;

    /** @brief Pointers to task queues this worker can process */
    std::vector<tasks *> task_queues;

    /** @brief Bit mask defining which execution queues this worker can process */
    execution_thread_mask mask;

    /** @brief Num empty worker loops to do before going to sleep on notifier */
    std::size_t num_loops_before_sleep = 0;
  };

  /**
   * @brief Data structure for managing a single execution queue
   *
   * This struct represents a single execution queue and contains both the
   * queue itself and references to all worker threads that can process it.
   */
  struct task_queue {
    /** @brief The actual task queue containing pending tasks */
    tasks queue;

    /** @brief Pointers to worker threads that can execute tasks from this queue */
    std::vector<worker_thread_data *> workers_data;
  };

  /** @brief Array of task queues, one for each execution queue mark */
  std::unique_ptr<task_queue[]> _tasks_queues;

  /** @brief Pointers to task queues that the main thread can process */
  std::vector<tasks *> _main_thread_queues;

  /** @brief Array of worker thread data structures */
  std::unique_ptr<worker_thread_data[]> _thread_data;

  /** @brief ID of the main thread for identification purposes */
  std::thread::id _main_thread_id;

  /** @brief Bit mask defining which execution queues the main thread can process */
  const execution_thread_mask _main_thread_mask;

  /** @brief Number of worker threads in the system */
  const uint32_t _num_workers;

  /** @brief Maximum execution queue mark that this system can handle */
  const execution_queue_mark _max_q;

  /** @brief Atomic flag indicating whether the system is in the process of shutting down */
  std::atomic_bool _is_stopping{false};
};

}  // namespace async_coro
