#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

TEST(execution_system, create_no_workers) {
  using namespace async_coro;

  execution_system system{{}};
}

TEST(execution_system, create_one_workers) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}}};
}

TEST(execution_system, create_five_workers) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker1", execution_queues::worker},
                                              {"worker2", execution_queues::worker},
                                              {"worker3", execution_queues::worker},
                                              {"worker4", execution_queues::worker},
                                              {"worker5", execution_queues::worker}}}};
}

TEST(execution_system, execute_or_plan_main) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  bool executed{false};
  const auto main_thread_id = std::this_thread::get_id();

  system.execute_or_plan_execution(
      [&] {
        EXPECT_EQ(std::this_thread::get_id(), main_thread_id);
        executed = true;
      },
      execution_queues::main);

  EXPECT_TRUE(executed);
}

TEST(execution_system, plan_main) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  bool executed{false};
  const auto main_thread_id = std::this_thread::get_id();

  system.plan_execution(
      [&] {
        EXPECT_EQ(std::this_thread::get_id(), main_thread_id);
        executed = true;
      },
      execution_queues::main);

  EXPECT_FALSE(executed);

  system.update_from_main();

  EXPECT_TRUE(executed);
}

TEST(execution_system, execute_or_plan_worker) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  std::atomic_bool executed{false};
  const auto main_thread_id = std::this_thread::get_id();

  system.execute_or_plan_execution(
      [&] {
        executed = true;

        EXPECT_NE(std::this_thread::get_id(), main_thread_id);
      },
      execution_queues::worker);

  std::this_thread::sleep_for(std::chrono::milliseconds{30});
  EXPECT_TRUE(executed);
}

TEST(execution_system, plan_execution_delayed_main) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  bool executed{false};
  const auto main_thread_id = std::this_thread::get_id();

  system.plan_execution(
      [&] {
        EXPECT_EQ(std::this_thread::get_id(), main_thread_id);
        executed = true;
      },
      execution_queues::main, std::chrono::steady_clock::now() + std::chrono::milliseconds{50});

  // should not have executed immediately
  EXPECT_FALSE(executed);

  // wait until after the scheduled time and then process main queue
  std::this_thread::sleep_for(std::chrono::milliseconds{70});
  system.update_from_main();

  EXPECT_TRUE(executed);
}

TEST(execution_system, plan_execution_delayed_worker) {
  using namespace async_coro;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  std::atomic_bool executed{false};
  const auto main_thread_id = std::this_thread::get_id();

  system.plan_execution(
      [&] {
        executed = true;
        EXPECT_NE(std::this_thread::get_id(), main_thread_id);
      },
      execution_queues::worker, std::chrono::steady_clock::now() + std::chrono::milliseconds{30});

  std::this_thread::sleep_for(std::chrono::milliseconds{80});
  EXPECT_TRUE(executed);
}

TEST(execution_system, delayed_multiple_diff_time_order_main) {
  using namespace async_coro;

  std::vector<int> order;

  execution_system system{{.worker_configs = {}, .main_thread_allowed_tasks = execution_queues::main}};

  const auto now = std::chrono::steady_clock::now();

  for (int i = 0; i < 5; ++i) {
    system.plan_execution([i, &order] {
      order.push_back(i);
    },
                          execution_queues::main, now + std::chrono::milliseconds{50 + (10 * i)});
  }

  // wait past scheduled time
  std::this_thread::sleep_for(std::chrono::milliseconds{80});

  // drain main queue until all executed
  for (int tries = 0; tries < 10 && static_cast<int>(order.size()) < 5; ++tries) {
    system.update_from_main();
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  ASSERT_EQ(order.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(order[i], i);
  }
}

TEST(execution_system, delayed_multiple_diff_time_order_worker) {
  using namespace async_coro;

  std::vector<int> order;
  std::mutex order_m;

  execution_system system{{.worker_configs = {{"worker", execution_queues::worker}}, .main_thread_allowed_tasks = execution_queues::main}};

  const auto now = std::chrono::steady_clock::now();

  // schedule with increasing offsets
  for (int i = 0; i < 5; ++i) {
    system.plan_execution([i, &order, &order_m] {
      std::scoped_lock lock(order_m);
      order.push_back(i);
    },
                          execution_queues::worker, now + std::chrono::milliseconds{100 - (10 * i)});
  }

  // wait enough time for all tasks to execute
  std::this_thread::sleep_for(std::chrono::milliseconds{200});

  ASSERT_EQ(order.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(order[i], 4 - i);  // reverse order because of time points
  }
}
