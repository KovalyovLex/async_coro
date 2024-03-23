#pragma once

#include <async_coro/internal/await_start_task.h>

namespace async_coro {

// Schedules parallel task in scheduller assotiated with this coroutine and returns task handle
template <typename R>
auto start_task(task<R> task, execution_thread thread = execution_thread::main_thread) {
  return internal::await_start_task(std::move(task), thread);
}

}  // namespace async_coro
