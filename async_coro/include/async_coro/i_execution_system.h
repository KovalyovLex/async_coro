#pragma once

#include <async_coro/execution_queue_mark.h>
#include <async_coro/unique_function.h>

#include <memory>

namespace async_coro {

class i_execution_system {
 public:
  // use function with Small Function Optimization buffer of 3 pointers
  using task_function = unique_function<void(), sizeof(void *) * 3>;

  using ptr = std::unique_ptr<i_execution_system>;

  i_execution_system() noexcept = default;
  virtual ~i_execution_system() noexcept = default;

  /// @brief Plans function for execution
  /// @param f function to execute
  /// @param execution_queue execution queue marker
  virtual void plan_execution(task_function f, execution_queue_mark execution_queue) = 0;

  /// @brief Checks if current thread is one of the workers or main and execution_queue fits config masks for that threads - executes f immediately.
  ///  Plan execution otherwise
  /// @param f function to execute
  /// @param execution_queue execution queue marker
  virtual void execute_or_plan_execution(task_function f, execution_queue_mark execution_queue) = 0;

  /// @brief Checks is current thread fits requested 'execution_queue'
  /// @param execution_queue execution queue marker
  /// @returns true if current thread is one of the workers or main and execution_queue fits configuration of system
  virtual bool is_current_thread_fits(execution_queue_mark execution_queue) const noexcept = 0;
};

}  // namespace async_coro