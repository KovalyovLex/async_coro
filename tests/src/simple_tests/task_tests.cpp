#include <async_coro/await_callback.h>
#include <async_coro/scheduler.h>
#include <async_coro/switch_thread.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>

namespace task_tests {
struct coro_runner {
  template <typename T>
  void run_coroutine(async_coro::task<T> coro) {
    _scheduler.start_task(std::move(coro));
  }

  async_coro::scheduler _scheduler;
};
}  // namespace task_tests

TEST(task, await_no_wait) {
  auto routine_1 = []() -> async_coro::task<float> { co_return 45.456f; };

  auto routine_2 = []() -> async_coro::task<int> { co_return 2; };

  auto routine = [](auto start1, auto start2) -> async_coro::task<int> {
    [[maybe_unused]] const auto res1 = co_await start1();
    auto routine1 = start1();
    [[maybe_unused]] auto res2 = co_await std::move(routine1);
    const auto res = co_await start2();
    co_return res;
  }(routine_1, routine_2);

  async_coro::scheduler scheduler;

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 2);
  EXPECT_EQ((int)handle, 2);
}

TEST(task, resume_on_callback_deep) {
  std::function<void()> continue_f;

  auto routine_1 = [&continue_f]() -> async_coro::task<float> {
    co_await async_coro::await_callback(
        [&continue_f](auto f) { continue_f = std::move(f); });
    co_return 45.456f;
  };

  auto routine_2 = [routine_1]() -> async_coro::task<int> {
    const auto res = co_await routine_1();
    co_return (int)(res);
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto res = co_await start();
    co_return res;
  }(routine_2);

  async_coro::scheduler scheduler;

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  continue_f();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 45);
}

TEST(task, resume_on_callback) {
  std::function<void()> continue_f;

  auto routine = [](auto& cnt) -> async_coro::task<int> {
    co_await async_coro::await_callback([&cnt](auto f) { cnt = std::move(f); });
    co_return 3;
  }(continue_f);

  async_coro::scheduler scheduler;

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  continue_f();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, resume_on_callback_reuse) {
  std::function<void()> continue_f;

  auto routine = [](auto& cnt) -> async_coro::task<int> {
    auto await =
        async_coro::await_callback([&cnt](auto f) { cnt = std::move(f); });
    co_await await;

    co_await await;

    co_return 2;
  }(continue_f);

  async_coro::scheduler scheduler;

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  std::exchange(continue_f, {})();
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  std::exchange(continue_f, {})();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 2);
}

TEST(task, async_execution) {
  static std::atomic_bool async_done = false;

  auto routine_1 = []() -> async_coro::task<float> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_thread(async_coro::execution_thread::worker_thread);

    EXPECT_NE(current_th, std::this_thread::get_id());

    async_done = true;

    co_await switch_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return 2.34f;
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto current_th = std::this_thread::get_id();

    auto res = co_await start();

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return (int) res;
  }(routine_1);

  async_coro::scheduler scheduler;
  scheduler.get_working_queue().set_num_threads(2);

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());

  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  ASSERT_FALSE(handle.done());

  if (!async_done) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  ASSERT_TRUE(async_done);

  scheduler.get_working_queue().set_num_threads(1);

  scheduler.update();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 2);
}

TEST(task, async_no_switch) {
  static std::atomic_bool async_done = false;

  auto routine_1 = []() -> async_coro::task<float> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_thread(async_coro::execution_thread::worker_thread);

    EXPECT_NE(current_th, std::this_thread::get_id());

    co_await switch_thread(async_coro::execution_thread::worker_thread);

    async_done = true;

    co_await switch_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_await switch_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return 7.14f;
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    auto res = co_await start();

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return (int) res;
  }(routine_1);

  async_coro::scheduler scheduler;
  scheduler.get_working_queue().set_num_threads(1);

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());

  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  ASSERT_FALSE(handle.done());

  if (!async_done) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  ASSERT_TRUE(async_done);

  scheduler.update();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 7);
}
