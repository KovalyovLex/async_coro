#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>
#include <async_coro/task_handle.h>
#include <async_coro/task_launcher.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {

template <typename R>
struct await_start_task {
  explicit await_start_task(task_launcher<R> tsk) noexcept
      : _task(std::move(tsk)) {}

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

  await_start_task& coro_await_transform(base_handle& parent) {
    _handle = parent.get_scheduler().start_task(std::move(_task));
    return *this;
  }

 private:
  task_launcher<R> _task;
  task_handle<R> _handle;
};

template <typename R>
await_start_task(task_launcher<R>) -> await_start_task<R>;

}  // namespace async_coro::internal
