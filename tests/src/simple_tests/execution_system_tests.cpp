#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

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
