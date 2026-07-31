#pragma once

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <server/io/io_uring_reactor.h>

#include <chrono>

namespace test_utils {

#if IO_URING_ENABLED

/**
 * @brief Helper to run a coroutine task with the io_uring reactor.
 *
 * Drives both the scheduler's execution system and the io_uring reactor's
 * event loop until the task finishes or the iteration budget is exhausted.
 *
 * @param task      The coroutine to execute.
 * @param scheduler Reference to the async_coro scheduler.
 * @param reactor   Reference to the io_uring reactor.
 * @return true if the task completed within the budget, false otherwise.
 */
inline bool run_task_io_uring(async_coro::task<int> task,
                              async_coro::scheduler& scheduler,
                              server::io::io_uring_reactor& reactor) {
  auto handle = scheduler.start_task(std::move(task), async_coro::execution_queues::main);
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
    reactor.process_loop(std::chrono::milliseconds(1));  // 1 ms
  }
  return handle.done();
}

#endif

}  // namespace test_utils
