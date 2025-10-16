#include <async_coro/await/sleep.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

TEST(sleep_tests, duration_and_resume_on_parent_queue) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 40ms;

  std::atomic<bool> done{false};
  std::atomic<long long> elapsed_ms{0};
  std::atomic<std::size_t> resumed_tid_hash{0};

  auto t = [&]() -> async_coro::task<> {
    auto start = std::chrono::steady_clock::now();
    co_await async_coro::sleep(sleep_dur);
    auto end = std::chrono::steady_clock::now();
    elapsed_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), std::memory_order::relaxed);
    resumed_tid_hash.store(std::hash<std::thread::id>{}(std::this_thread::get_id()), std::memory_order::relaxed);
    done.store(true, std::memory_order::release);
  };

  auto main_tid = std::this_thread::get_id();
  auto h = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !done.load(std::memory_order::relaxed); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(done.load(std::memory_order::acquire));
  auto got_ms = elapsed_ms.load(std::memory_order::relaxed);
  EXPECT_GE(got_ms, sleep_dur.count() - 5);
  EXPECT_EQ(resumed_tid_hash.load(std::memory_order::relaxed), std::hash<std::thread::id>{}(main_tid));
}

TEST(sleep_tests, resume_on_worker_queue) {
  using namespace std::chrono_literals;

  // create execution system with a worker so worker queue exists
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  constexpr auto sleep_dur = 40ms;

  std::atomic<bool> done{false};
  std::atomic<long long> elapsed_ms{0};
  std::atomic<std::size_t> resumed_tid_hash{0};

  auto t = [&]() -> async_coro::task<> {
    auto start = std::chrono::steady_clock::now();
    co_await async_coro::sleep(sleep_dur, async_coro::execution_queues::worker);
    auto end = std::chrono::steady_clock::now();
    elapsed_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), std::memory_order::relaxed);
    resumed_tid_hash.store(std::hash<std::thread::id>{}(std::this_thread::get_id()), std::memory_order::relaxed);
    done.store(true, std::memory_order::release);
  };

  auto main_tid = std::this_thread::get_id();
  auto h = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !done.load(std::memory_order::relaxed); ++i) {
    std::this_thread::sleep_for(1ms);
    // update main to process continuations coming back to main
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(done.load(std::memory_order::acquire));
  auto got_ms = elapsed_ms.load(std::memory_order::relaxed);
  EXPECT_GE(got_ms, sleep_dur.count() - 5);
  // resumed thread should not be main thread (worker thread)
  EXPECT_NE(resumed_tid_hash.load(std::memory_order::relaxed), std::hash<std::thread::id>{}(main_tid));
}
