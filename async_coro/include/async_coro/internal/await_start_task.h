#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {

template <typename R>
struct await_start_task {
  explicit await_start_task(task<R> tsk, execution_queue_mark execution_queue) noexcept
      : _task(std::move(tsk)),
        _execution_queue(execution_queue) {}

  await_start_task(const await_start_task&) = delete;
  await_start_task(await_start_task&&) = delete;

  await_start_task& operator=(await_start_task&&) = delete;
  await_start_task& operator=(const await_start_task&) = delete;

  bool await_ready() const noexcept { return true; }

  template <typename U>
    requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U>) noexcept {
    ASYNC_CORO_ASSERT(false);  // we should newer suspend this immediate coroutine
  }

  auto await_resume() noexcept {
    return std::move(_handle);
  }

  void embed_task(base_handle& parent) noexcept {
    _handle = parent.get_scheduler().start_task(std::move(_task), _execution_queue);
  }

 private:
  task<R> _task;
  task_handle<R> _handle;
  execution_queue_mark _execution_queue;
};

template <typename R>
await_start_task(task<R>, execution_queue_mark) -> await_start_task<R>;

}  // namespace async_coro::internal
