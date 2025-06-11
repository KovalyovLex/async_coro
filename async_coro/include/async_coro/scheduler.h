#pragma once

#include <async_coro/config.h>
#include <async_coro/i_execution_system.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/task.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/unique_function.h>

#include <vector>

namespace async_coro {

template <typename R>
struct task;

class base_handle;

class scheduler {
 public:
  scheduler();
  explicit scheduler(i_execution_system::ptr system) noexcept;
  ~scheduler();

  // Schedules task and starts it execution
  template <typename R>
  task_handle<R> start_task(task<R> coro,
                            execution_queue_mark execution_queue = execution_queues::main) {
    auto handle = coro.release_handle(internal::passkey{this});
    task_handle<R> result{handle};
    if (!handle.done()) [[likely]] {
      add_coroutine(handle.promise(), execution_queue);
      return result;
    }
    // free task as we released handle
    handle.promise().on_task_freed_by_scheduler();
    return result;
  }

  template <class T>
    requires(std::derived_from<T, i_execution_system>)
  T& get_execution_system() const noexcept {
    ASYNC_CORO_ASSERT(dynamic_cast<T*>(_execution_system.get()));

    return *static_cast<T*>(_execution_system.get());
  }

 public:
  // for internal api use

  // If current thread the same as last executed - continues execution immediately or plans execution on queue
  void continue_execution(base_handle& handle_impl);

  // Only plans execution on thread that was assigned for the coro. So continue will be called later
  void plan_continue_execution(base_handle& handle_impl);

  // Plans execution on execution_queue.
  void change_execution_queue(base_handle& handle_impl, execution_queue_mark execution_queue);

  // Embed coroutine
  void on_child_coro_added(base_handle& parent, base_handle& child, internal::passkey<task_base>);

 private:
  bool is_current_thread_fits(execution_queue_mark execution_queue) noexcept;
  void add_coroutine(base_handle& handle_impl, execution_queue_mark execution_queue);
  void continue_execution_impl(base_handle& handle_impl);
  void plan_continue_on_thread(base_handle& handle_impl, execution_queue_mark execution_queue);

 private:
  mutex _mutex;
  i_execution_system::ptr _execution_system;
  std::vector<base_handle*> CORO_THREAD_GUARDED_BY(_mutex) _managed_coroutines;
  bool _is_destroying CORO_THREAD_GUARDED_BY(_mutex) = false;
};

}  // namespace async_coro
