#pragma once

#include <async_coro/callback.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/task.h>

#include <memory>
#include <type_traits>

namespace async_coro {

template <typename R>
class task_launcher {
 public:
  task_launcher(callback<task<R>>::ptr start_function, execution_queue_mark execution_queue) noexcept
      : _start_function(std::move(start_function)),
        _coro(typename task<R>::handle_type(nullptr)),
        _execution_queue(execution_queue) {}

  task_launcher(callback<task<R>>::ptr start_function) noexcept
      : task_launcher(std::move(start_function), execution_queues::main) {}

  task_launcher(callback_noexcept<task<R>>::ptr start_function, execution_queue_mark execution_queue) noexcept
      : _start_function(reinterpret_cast<callback<task<R>>*>(start_function.release())),
        _coro(typename task<R>::handle_type(nullptr)),
        _execution_queue(execution_queue) {}

  task_launcher(callback_noexcept<task<R>>::ptr start_function) noexcept
      : task_launcher(std::move(start_function), execution_queues::main) {}

  template <typename T>
    requires(std::is_invocable_r_v<task<R>, T>)
  task_launcher(T&& start_function, execution_queue_mark execution_queue) noexcept
      : task_launcher(allocate_callback(std::forward<T>(start_function)), execution_queue) {}

  template <typename T>
    requires(std::is_invocable_r_v<task<R>, T>)
  task_launcher(T&& start_function) noexcept
      : task_launcher(std::forward<T>(start_function), execution_queues::main) {}

  task_launcher(task<R> coro, execution_queue_mark execution_queue) noexcept
      : _coro(std::move(coro)), _execution_queue(execution_queue) {}

  task_launcher(task<R> coro) noexcept
      : task_launcher(std::move(coro), execution_queues::main) {}

  task<R> launch() {
    if (_start_function) {
      return _start_function->execute();
    }
    return std::move(_coro);
  }

  callback_base::ptr get_start_function() noexcept {
    return callback_base::ptr{_start_function.release()};
  }

  execution_queue_mark get_execution_queue() const noexcept {
    return _execution_queue;
  }

 private:
  callback<task<R>>::ptr _start_function;
  task<R> _coro;
  execution_queue_mark _execution_queue;
};

template <typename R>
task_launcher(std::unique_ptr<callback<task<R>>, callback_base::deleter>) -> task_launcher<R>;
template <typename R>
task_launcher(std::unique_ptr<callback<task<R>>, callback_base::deleter>, execution_queue_mark) -> task_launcher<R>;

template <typename R>
task_launcher(std::unique_ptr<callback_noexcept<task<R>>, callback_base::deleter>) -> task_launcher<R>;
template <typename R>
task_launcher(std::unique_ptr<callback_noexcept<task<R>>, callback_base::deleter>, execution_queue_mark) -> task_launcher<R>;

template <typename R>
task_launcher(task<R>) -> task_launcher<R>;
template <typename R>
task_launcher(task<R>, execution_queue_mark) -> task_launcher<R>;

template <typename T>
  requires(std::is_invocable_v<T> && internal::is_task_v<std::invoke_result_t<T>>)
task_launcher(T) -> task_launcher<internal::unwrap_task_t<std::invoke_result_t<T>>>;

template <typename T>
  requires(std::is_invocable_v<T> && internal::is_task_v<std::invoke_result_t<T>>)
task_launcher(T, execution_queue_mark) -> task_launcher<internal::unwrap_task_t<std::invoke_result_t<T>>>;

}  // namespace async_coro