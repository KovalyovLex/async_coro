#include <async_coro/await/execute_after_time.h>
#include <async_coro/await/sleep.h>
#include <async_coro/await/start_task.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

TEST(execute_after_time_tests, executes_function_after_sleep_duration) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 40ms;
  std::atomic<long long> elapsed_ms{0};
  std::atomic<int> result{0};

  auto t = [&]() -> async_coro::task<> {
    auto start = std::chrono::steady_clock::now();
    int res = co_await async_coro::execute_after_time([]() { return 42; }, sleep_dur);
    auto end = std::chrono::steady_clock::now();
    elapsed_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), std::memory_order::relaxed);
    result.store(res, std::memory_order::relaxed);
  };

  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(result.load(std::memory_order::relaxed), 42);
  auto got_ms = elapsed_ms.load(std::memory_order::relaxed);
  EXPECT_GE(got_ms, sleep_dur.count() - 5);
}

TEST(execute_after_time_tests, executes_function_returning_void) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 30ms;
  std::atomic<bool> function_executed{false};

  auto t = [&]() -> async_coro::task<> {
    co_await async_coro::execute_after_time([&]() { function_executed.store(true, std::memory_order::relaxed); }, sleep_dur);
  };

  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_TRUE(function_executed.load(std::memory_order::relaxed));
}

TEST(execute_after_time_tests, resumes_on_parent_queue) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 40ms;

  std::atomic<std::size_t> resumed_tid_hash{0};

  auto t = [&]() -> async_coro::task<> {
    co_await async_coro::execute_after_time([]() { return 0; }, sleep_dur);
    resumed_tid_hash.store(std::hash<std::thread::id>{}(std::this_thread::get_id()), std::memory_order::relaxed);
  };

  auto main_tid = std::this_thread::get_id();
  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(resumed_tid_hash.load(std::memory_order::relaxed), std::hash<std::thread::id>{}(main_tid));
}

TEST(execute_after_time_tests, multiple_executions_in_sequence) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 20ms;
  std::atomic<int> counter{0};

  auto t = [&]() -> async_coro::task<> {
    int res1 = co_await async_coro::execute_after_time([&]() { return ++counter; }, sleep_dur);
    int res2 = co_await async_coro::execute_after_time([&]() { return ++counter; }, sleep_dur);
    int res3 = co_await async_coro::execute_after_time([&]() { return ++counter; }, sleep_dur);

    EXPECT_EQ(res1, 1);
    EXPECT_EQ(res2, 2);
    EXPECT_EQ(res3, 3);
  };

  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout (need more time for 3 sequential waits)
  for (int i = 0; i < 3000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(counter.load(std::memory_order::relaxed), 3);
}

TEST(execute_after_time_tests, returns_multiple_values_with_tuple) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 30ms;
  std::atomic<int> first_val{0};
  std::atomic<const char*> second_val{"unset"};

  auto t = [&]() -> async_coro::task<> {
    auto [a, b] = co_await async_coro::execute_after_time([]() { return std::make_tuple(10, std::string_view{"hello"}); }, sleep_dur);
    first_val.store(a, std::memory_order::relaxed);
    second_val.store(b.data(), std::memory_order::relaxed);  // NOLINT(*data*)
  };

  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(first_val.load(std::memory_order::relaxed), 10);
  EXPECT_STREQ(second_val.load(std::memory_order::relaxed), "hello");
}

TEST(execute_after_time_tests, when_any_with_start_task) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  std::atomic<int> result{0};

  auto quick_task = []() -> async_coro::task<int> {
    co_await async_coro::sleep(5ms);
    co_return 100;
  };

  auto parent = [&]() -> async_coro::task<> {
    // The quick_task should complete before execute_after_time's 100ms delay
    auto res = co_await (co_await async_coro::start_task(quick_task()) || async_coro::execute_after_time([]() { return 42; }, 100ms));
    result.store(std::visit([](int r) { return r; }, res), std::memory_order::relaxed);
  };

  auto handle = scheduler.start_task(parent());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(result.load(std::memory_order::relaxed), 100);
}

TEST(execute_after_time_tests, when_any_execute_after_time_wins) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  std::atomic<int> result{0};

  auto slow_task = []() -> async_coro::task<int> {
    co_await async_coro::sleep(300ms);
    co_return 100;
  };

  auto parent = [&]() -> async_coro::task<> {
    // execute_after_time should win with 20ms vs 300ms sleep of slow_task
    auto res = co_await (co_await async_coro::start_task(slow_task()) || async_coro::execute_after_time([]() { return 42; }, 20ms));
    result.store(std::visit([](int r) { return r; }, res), std::memory_order::relaxed);
  };

  auto handle = scheduler.start_task(parent());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(result.load(std::memory_order::relaxed), 42);
}

TEST(execute_after_time_tests, lambda_captures_work_correctly) {
  using namespace std::chrono_literals;

  async_coro::scheduler scheduler;

  constexpr auto sleep_dur = 30ms;
  std::atomic<int> result{0};

  auto t = [&]() -> async_coro::task<> {
    int captured_val = 99;
    int res = co_await async_coro::execute_after_time([captured_val]() { return captured_val * 2; }, sleep_dur);
    result.store(res, std::memory_order::relaxed);
  };

  auto handle = scheduler.start_task(t());

  // wait for completion with a timeout
  for (int i = 0; i < 2000 && !handle.done(); ++i) {
    std::this_thread::sleep_for(1ms);
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(result.load(std::memory_order::relaxed), 198);
}
