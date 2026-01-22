#pragma once

#include <async_coro/execution_queue_mark.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/task.h>
#include <async_coro/utils/allocate_callback.h>
#include <async_coro/utils/callback_ptr.h>

#include <type_traits>

namespace async_coro {

/**
 * @brief A launcher for asynchronous tasks that can be executed on specific execution queues.
 *
 * The task_launcher class provides a mechanism to create and launch asynchronous tasks
 * with control over which execution queue they run on. It can be constructed with either
 * a callback function that returns a task, or directly with a task object.
 *
 * @tparam R The return type of the task being launched
 */
template <typename R>
class task_launcher {
 public:
  /**
   * @brief Constructs a task launcher with a callback function and execution queue.
   *
   * @param start_function A callback function that returns a task<R> when executed
   * @param execution_queue The execution queue where the task should be scheduled
   */
  task_launcher(callback_ptr<task<R>()> start_function, execution_queue_mark execution_queue) noexcept
      : _start_function(std::move(start_function)),
        _coro(typename task<R>::handle_type(nullptr)),
        _execution_queue(execution_queue) {}

  /**
   * @brief Constructs a task launcher with a callback function on the main execution queue.
   *
   * @param start_function A callback function that returns a task<R> when executed
   */
  explicit task_launcher(callback_ptr<task<R>()> start_function) noexcept
      : task_launcher(std::move(start_function), execution_queues::main) {}

  /**
   * @brief Constructs a task launcher with a noexcept callback function and execution queue.
   *
   * @param start_function A noexcept callback function that returns a task<R> when executed
   * @param execution_queue The execution queue where the task should be scheduled
   */
  task_launcher(callback_ptr<task<R>() noexcept> start_function, execution_queue_mark execution_queue) noexcept
      : _start_function(reinterpret_cast<callback<task<R>()>*>(start_function.release())),  // NOLINT(*-reinterpret-cast)
        _coro(typename task<R>::handle_type(nullptr)),
        _execution_queue(execution_queue) {}

  /**
   * @brief Constructs a task launcher with a noexcept callback function on the main execution queue.
   *
   * @param start_function A noexcept callback function that returns a task<R> when executed
   */
  explicit task_launcher(callback_ptr<task<R>() noexcept> start_function) noexcept
      : task_launcher(std::move(start_function), execution_queues::main) {}

  /**
   * @brief Constructs a task launcher with any callable that returns a task<R> and execution queue.
   *
   * This constructor accepts any callable object (function, lambda, member function, etc.)
   * that returns a task<R> when invoked.
   *
   * @param start_function A callable that returns a task<R> when invoked
   * @param execution_queue The execution queue where the task should be scheduled
   */
  template <typename T>
    requires(std::is_invocable_r_v<task<R>, T> && !std::is_convertible_v<T &&, task<R> (*)()>)
  task_launcher(T&& start_function, execution_queue_mark execution_queue)
      : task_launcher(allocate_callback(std::forward<T>(start_function)), execution_queue) {}

  /**
   * @brief Constructs a task launcher with any callable that returns a task<R> and execution queue.
   *
   * This constructor accepts static functions that returns a task<R> when invoked.
   *
   * @param start_function A callable that returns a task<R> when invoked
   * @param execution_queue The execution queue where the task should be scheduled
   */
  template <typename T>
    requires(std::is_invocable_r_v<task<R>, T> && std::is_convertible_v<T, task<R> (*)()>)
  task_launcher(const T& start_function, execution_queue_mark execution_queue)
      : task_launcher(start_function(), execution_queue) {}

  /**
   * @brief Constructs a task launcher with any callable that returns a task<R> on the main execution queue.
   *
   * This constructor accepts any callable object (function, lambda, member function, etc.)
   * that returns a task<R> when invoked.
   *
   * @param start_function A callable that returns a task<R> when invoked
   */
  template <typename T>
    requires(std::is_invocable_r_v<task<R>, T>)
  explicit task_launcher(T&& start_function)
      : task_launcher(std::forward<T>(start_function), execution_queues::main) {}

  /**
   * @brief Constructs a task launcher with an existing task and execution queue.
   *
   * @param coro An existing task<R> object to be launched
   * @param execution_queue The execution queue where the task should be scheduled
   */
  task_launcher(task<R> coro, execution_queue_mark execution_queue) noexcept
      : _coro(std::move(coro)), _execution_queue(execution_queue) {}

  /**
   * @brief Constructs a task launcher with an existing task on the main execution queue.
   *
   * @param coro An existing task<R> object to be launched
   */
  explicit task_launcher(task<R> coro) noexcept
      : task_launcher(std::move(coro), execution_queues::main) {}

  /**
   * @brief Launches the task and returns it.
   *
   * If the launcher was constructed with a callback function, this method executes
   * the callback to create and return the task. If the launcher was constructed
   * with an existing task, this method returns that task.
   *
   * @return The task<R> to be executed
   */
  task<R> launch() {
    if (_start_function) {
      return _start_function.execute();
    }
    return std::move(_coro);
  }

  /**
   * @brief Retrieves the start function callback.
   *
   * @return A pointer to the callback base, or nullptr if no callback was set
   */
  callback_base_ptr<false> get_start_function() noexcept {
    return std::move(_start_function);
  }

  /**
   * @brief Gets the execution queue mark for this launcher.
   *
   * @return The execution queue where tasks from this launcher will be scheduled
   */
  [[nodiscard]] execution_queue_mark get_execution_queue() const noexcept {
    return _execution_queue;
  }

 private:
  callback_ptr<task<R>()> _start_function = nullptr;
  task<R> _coro;
  execution_queue_mark _execution_queue;
};

template <typename R>
task_launcher(callback_ptr<task<R>()>) -> task_launcher<R>;
template <typename R>
task_launcher(callback_ptr<task<R>()>, execution_queue_mark) -> task_launcher<R>;

template <typename R>
task_launcher(callback_ptr<task<R>() noexcept>) -> task_launcher<R>;
template <typename R>
task_launcher(callback_ptr<task<R>() noexcept>, execution_queue_mark) -> task_launcher<R>;

template <typename R>
task_launcher(task<R>) -> task_launcher<R>;
template <typename R>
task_launcher(task<R>, execution_queue_mark) -> task_launcher<R>;

template <typename T>
  requires(std::is_invocable_v<T> && internal::is_task_v<std::invoke_result_t<T>>)
task_launcher(T&&) -> task_launcher<internal::unwrap_task_t<std::invoke_result_t<T>>>;

template <typename T>
  requires(std::is_invocable_v<T> && internal::is_task_v<std::invoke_result_t<T>>)
task_launcher(T&&, execution_queue_mark) -> task_launcher<internal::unwrap_task_t<std::invoke_result_t<T>>>;

template <typename... TArgs>
concept is_task_launchable = requires(TArgs&&... task) { task_launcher{std::forward<TArgs>(task)...}; };

}  // namespace async_coro
