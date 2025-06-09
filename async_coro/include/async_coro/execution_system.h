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

struct execution_thread_config {
  execution_thread_config(std::string name) : name(std::move(name)) {}
  execution_thread_config(std::string name, execution_thread_mask m) : name(std::move(name)), allowed_tasks(m) {}
  execution_thread_config(std::string name, execution_queue_mark m) : name(std::move(name)), allowed_tasks(m) {}

  std::string name;
  execution_thread_mask allowed_tasks = execution_queues::worker | execution_queues::any;
};

struct execution_system_config {
  std::vector<execution_thread_config> worker_configs;
  execution_thread_mask main_thread_allowed_tasks = execution_queues::main | execution_queues::any;
};

class execution_system : public i_execution_system {
 public:
  /// Creates execution system instance with worker threads equals to num of worker_configs in config
  /// Should be created only from "main" thread that will call update_from_main()
  execution_system(const execution_system_config &config, execution_queue_mark max_queue = execution_queues::any);
  execution_system(const execution_system &) = delete;
  execution_system(execution_system &&) = delete;
  ~execution_system() noexcept override;

  execution_system &operator=(const execution_system &) = delete;
  execution_system &operator=(execution_system &&) = delete;

  void plan_execution(task_function f, execution_queue_mark execution_queue) override;
  void execute_or_plan_execution(task_function f, execution_queue_mark execution_queue) override;
  bool is_current_thread_fits(execution_queue_mark execution_queue) const noexcept override;

  /// @brief Trying to execute one task for main thread
  void update_from_main();

  /// @brief Returns current number or worker threads
  /// @return num_threads
  uint32_t get_num_worker_threads() const noexcept { return _num_workers; }

  /// @brief Returns number or workers that can process this queue (including main thread)
  /// @param execution_queue execution queue marker
  /// @return num_threads
  uint32_t get_num_workers_for_queue(execution_queue_mark execution_queue) const noexcept;

 private:
  struct worker_thread_data;

  void worker_loop(worker_thread_data &data);

  static void set_thread_name(std::thread &thread, const std::string &name);

 private:
  using tasks = atomic_queue<task_function>;

  struct worker_thread_data {
    std::thread thread;
    std::vector<tasks *> task_queues;
    execution_thread_mask mask;
    thread_notifier notifier;
  };

  struct task_queue {
    tasks queue;

    // workers that can execute tasks in this queue
    std::vector<worker_thread_data *> workers_data;
  };

  std::unique_ptr<task_queue[]> _tasks_queues;
  std::vector<tasks *> _main_thread_queues;
  std::unique_ptr<worker_thread_data[]> _thread_data;
  std::thread::id _main_thread_id;
  const execution_thread_mask _main_thread_mask;
  const uint32_t _num_workers;
  const execution_queue_mark _max_q;
  std::atomic_bool _is_stopping{false};
};

}  // namespace async_coro