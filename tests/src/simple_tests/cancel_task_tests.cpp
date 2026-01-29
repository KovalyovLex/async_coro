#include <async_coro/await/await_callback.h>
#include <async_coro/await/cancel.h>
#include <async_coro/await/sleep.h>
#include <async_coro/await/start_task.h>
#include <async_coro/await/switch_to_queue.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <async_coro/task_launcher.h>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <semaphore>
#include <thread>

// Tests that verify behavior of co_await async_coro::cancel() and cancellation
// propagation in combinations of awaitables (when_any || and when_all &&), and
// across execution queues.

TEST(cancel_task, await_cancel_direct) {
  auto child = []() -> async_coro::task<int> {
    // schedule cancellation of parent when resumed
    co_await async_coro::cancel();

    // should not reach here
    ADD_FAILURE();

    co_return 1;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // start child and then request cancel from inside child via cancel()
    auto h = co_await child();

    // should not reach here
    ADD_FAILURE();

    co_return h;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(parent());
  // parent is suspended waiting for child
  ASSERT_FALSE(handle.done());

  // after cancellation parent should be finished (cancelled)
  ASSERT_TRUE(handle.is_cancelled());
}

TEST(cancel_task, when_any_cancel_others) {
  // long running task that should be cancelled by immediate task
  auto long_task = []() -> async_coro::task<int> {
    co_await async_coro::switch_to_queue(async_coro::execution_queues::worker);

    // run switch back after a short delay
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // we should suspend here and cancel
    co_await async_coro::switch_to_queue(async_coro::execution_queues::main);

    // should not reach here
    ADD_FAILURE();

    co_return 100;
  };

  auto immediate = []() -> async_coro::task<int> { co_return 1; };

  auto parent = [&]() -> async_coro::task<int> {
    // when_any: first completed should cancel others
    auto res = co_await ((co_await async_coro::start_task(long_task())) || (co_await async_coro::start_task(immediate())));
    // result variant should contain 1 (immediate) or long_task result; ensure parent finishes
    EXPECT_EQ(res.index(), 1);

    co_return 0;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(parent());
  ASSERT_TRUE(handle.done());

  // update main to process cancellations
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
}

TEST(cancel_task, when_all_parent_cancelled) {
  std::binary_semaphore sema{0};

  auto child1 = [&sema]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&sema](auto f) {
      std::thread([f = std::move(f), &sema]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        f();
        sema.release();
      }).detach();
    });
    // request cancel of parent
    co_await async_coro::cancel();
    co_return 5;
  };

  auto infinite_task = []() -> async_coro::task<int> {
    // should be cancelled by parent before finishing
    co_await async_coro::await_callback([](auto /*f*/) { /* never call */ });

    // should not reach here
    ADD_FAILURE();

    co_return 7;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // both children awaited with when_all - cancellation in one should cancel the whole group
    co_await (co_await async_coro::start_task(child1()) && co_await async_coro::start_task(infinite_task()));

    // should not reach here
    ADD_FAILURE();

    co_return 0;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(parent());
  EXPECT_FALSE(handle.done());
  EXPECT_FALSE(handle.is_cancelled());

  sema.acquire();

  // process cancellations
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  EXPECT_FALSE(handle.done());
  EXPECT_TRUE(handle.is_cancelled());
}

TEST(cancel_task, when_any_all_children_cancelled) {
  std::binary_semaphore sema{0};

  auto child1 = [&sema]() -> async_coro::task<int> {
    auto q_before = co_await async_coro::switch_to_queue(async_coro::execution_queues::worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    sema.release();

    co_await async_coro::switch_to_queue(q_before);

    // request cancel of parent
    co_await async_coro::cancel();
    co_return 5;
  };

  auto child2 = []() -> async_coro::task<int> {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    co_await async_coro::cancel();

    // should not reach here
    ADD_FAILURE();

    co_return 7;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // both children awaited with when_all - cancellation in one should cancel the whole group
    co_await (co_await async_coro::start_task(child1()) || co_await async_coro::start_task(child2()));

    // should not reach here
    ADD_FAILURE();

    co_return 0;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(parent());
  EXPECT_FALSE(handle.done());
  EXPECT_FALSE(handle.is_cancelled());

  sema.acquire();

  // process cancellations
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  EXPECT_FALSE(handle.done());
  EXPECT_TRUE(handle.is_cancelled());
}

TEST(cancel_task, cross_queue_cancel) {
  std::atomic_bool worker_done = false;

  auto worker_task = [&worker_done]() -> async_coro::task<> {
    // switch to worker queue and then request cancel of parent
    co_await async_coro::switch_to_queue(async_coro::execution_queues::worker);
    worker_done = true;
    co_await async_coro::cancel();
    co_return;
  };

  auto infinite_task = []() -> async_coro::task<void> {
    // will be cancelled by child while child is on worker
    co_await async_coro::await_callback([](auto /*f*/) { /* parent waits for child to cancel */ });

    // should not reach here
    ADD_FAILURE();
  };

  auto parent = [&]() -> async_coro::task<void> {
    co_await (co_await async_coro::start_task(worker_task()) || co_await async_coro::start_task(infinite_task()));

    // should not reach here
    ADD_FAILURE();
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(parent());

  // wait for worker run
  std::size_t tries = 0;
  while (!worker_done && ++tries < 1000) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(worker_done);

  // wait for cancel execute finish
  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  // process cancellation on main
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  EXPECT_FALSE(handle.done());

  // parent coroutine with any await can be cancelled when all children were cancelled or coroutine itself were cancelled
  EXPECT_FALSE(handle.is_cancelled());
}

TEST(cancel_task, root_request_cancel_with_embedded_children) {
  // Parent awaits children directly (embedded) and then parent is cancelled from outside
  std::atomic_bool child_started = false;

  auto infinite_task = [&child_started]() -> async_coro::task<int> {
    child_started = true;
    co_await async_coro::await_callback([](auto /*f*/) {});

    // should not reach here
    ADD_FAILURE();

    // if parent is cancelled this point must not be reached in parent logic
    co_return 10;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // awaiting child directly => embedded coroutine
    auto r = co_await infinite_task();
    (void)r;
    // should not be reached if parent cancelled before child resumes
    ADD_FAILURE();
    co_return 0;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(parent());

  // ensure child was started and embedded
  ASSERT_TRUE(child_started);

  // cancel root from outside
  handle.request_cancel();

  // parent should be cancelled
  ASSERT_TRUE(handle.is_cancelled());
}

TEST(cancel_task, root_request_cancel_with_started_children) {
  // Parent starts children via start_task (parallel) and then root is cancelled
  std::atomic_bool child1_resumed = false;
  std::atomic_bool child2_resumed = false;

  auto infinite_task1 = [&child1_resumed]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&](auto /*f*/) { child1_resumed = true; });

    // should not reach here
    ADD_FAILURE();

    co_return 1;
  };

  auto infinite_task2 = [&child2_resumed]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&](auto /*f*/) { child2_resumed = true; });

    // should not reach here
    ADD_FAILURE();

    co_return 2;
  };

  auto parent = [&]() -> async_coro::task<int> {
    // start children in parallel
    auto h1 = co_await async_coro::start_task(infinite_task1());
    auto h2 = co_await async_coro::start_task(infinite_task2());
    // wait for both (when_all)
    co_await (std::move(h1) && std::move(h2));
    // should not reach here if parent cancelled
    ADD_FAILURE();
    co_return 0;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(parent());

  // wait a bit for children to start
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // cancel root
  handle.request_cancel();

  // children should be requested to cancel (or parent cancelled)
  ASSERT_TRUE(handle.is_cancelled());
}

TEST(cancel_task, cancel_while_sleep_active) {
  // ensure cancellation cancels scheduled sleep and resumes coroutine via continue_after_sleep
  std::atomic_bool coro_resumed{false};

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto parent = [&]() -> async_coro::task<void> {
    co_await async_coro::sleep(std::chrono::milliseconds(200), async_coro::execution_queues::worker);
    coro_resumed.store(true, std::memory_order::relaxed);
  };

  {
    auto handle = scheduler.start_task(parent());

    // give child a moment to schedule its sleep on the worker
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // cancel root while child is sleeping
    handle.request_cancel();
    EXPECT_TRUE(handle.is_cancelled());
    EXPECT_FALSE(coro_resumed.load(std::memory_order::relaxed));

    // let worker/main process cancellation (if any)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

    EXPECT_TRUE(handle.is_cancelled());
  }

  // check coro wasn't resumed after destruction of handle
  EXPECT_FALSE(coro_resumed.load(std::memory_order::relaxed));
}

TEST(cancel_task, cancel_parent_while_sleep_active) {
  // ensure cancellation cancels scheduled sleep and resumes coroutine via continue_after_sleep
  std::atomic_bool child_resumed{false};

  auto child = [&child_resumed]() -> async_coro::task<void> {
    // sleep on worker queue to ensure work scheduled in execution system
    co_await async_coro::sleep(std::chrono::milliseconds(200), async_coro::execution_queues::worker);
    child_resumed.store(true, std::memory_order::relaxed);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto parent = [&]() -> async_coro::task<void> {
    // start child
    co_await child();

    // suspend parent indefinitely (will be cancelled from outside)
    co_await async_coro::await_callback([](auto /*f*/) {});

    co_return;
  };

  auto handle = scheduler.start_task(parent());

  // give child a moment to schedule its sleep on the worker
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // cancel root while child is sleeping
  handle.request_cancel();
  EXPECT_FALSE(child_resumed.load(std::memory_order::relaxed));

  // let worker/main process cancellation
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  EXPECT_TRUE(handle.is_cancelled());
  EXPECT_FALSE(child_resumed.load(std::memory_order::relaxed));
}
