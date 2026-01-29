#include <async_coro/await/await_callback.h>
#include <async_coro/await/cancel_after_time.h>
#include <async_coro/await/start_task.h>
#include <async_coro/await/switch_to_queue.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

TEST(cancel_after_time, when_any_triggers_cancel_on_main) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  auto infinite = []() -> async_coro::task<int> {
    co_await async_coro::await_callback([](auto /*f*/) { /* never call */ });
    // should not reach here
    ADD_FAILURE();
    co_return 1;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // cancel_after_time should fire and cancel the group
    co_await (co_await async_coro::start_task(infinite()) || async_coro::cancel_after_time(20ms));

    // should not reach here
    ADD_FAILURE();
    co_return 0;
  };

  auto handle = scheduler.start_task(parent());

  for (int i = 0; i < 2000 && !(handle.done() || handle.is_cancelled()); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  EXPECT_FALSE(handle.done());
  EXPECT_TRUE(handle.is_cancelled());
}

TEST(cancel_after_time, timer_scheduled_on_worker_queue) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto long_on_worker = []() -> async_coro::task<int> {
    auto prev_q = co_await async_coro::switch_to_queue(async_coro::execution_queues::worker);
    // make sure this task blocks for longer than the timer
    std::this_thread::sleep_for(50ms);

    co_await async_coro::switch_to_queue(prev_q);
    // should not reach here
    ADD_FAILURE();
    co_return 42;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // schedule timer on worker so cancellation logic runs there
    co_await (co_await async_coro::start_task(long_on_worker()) || async_coro::cancel_after_time(10ms, async_coro::execution_queues::main));

    // should not reach here
    ADD_FAILURE();
    co_return 0;
  };

  auto handle = scheduler.start_task(parent());

  for (int i = 0; i < 2000 && !(handle.done() || handle.is_cancelled()); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  EXPECT_FALSE(handle.done());
  EXPECT_TRUE(handle.is_cancelled());
}
