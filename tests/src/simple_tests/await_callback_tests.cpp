#include <async_coro/await/await_callback.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <async_coro/utils/unique_function.h>
#include <gtest/gtest.h>

#include <string>

TEST(await_callback, resume_on_callback_deep) {
  async_coro::unique_function<void()> continue_f;

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
  auto handle = scheduler.start_task(std::move(routine), async_coro::execution_queues::main);
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  continue_f();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 45);
}

TEST(await_callback, resume_on_callback) {
  async_coro::unique_function<void()> continue_f;

  auto routine = [](auto& cnt) -> async_coro::task<int> {  // NOLINT(*-reference-coroutine-*)
    co_await async_coro::await_callback([&cnt](auto f) { cnt = std::move(f); });
    co_return 3;
  }(continue_f);

  async_coro::scheduler scheduler;

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine), async_coro::execution_queues::main);
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  continue_f();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(await_callback, callback_arg_int) {
  async_coro::unique_function<void(int)> continue_f;

  auto routine_1 = [&continue_f]() -> async_coro::task<int> {
    const auto res = co_await async_coro::await_callback_with_result<int>(
        [&continue_f](auto f) { continue_f = std::move(f); });
    co_return res;
  };

  auto routine_2 = [routine_1]() -> async_coro::task<int> {
    co_return co_await routine_1();
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine_2, async_coro::execution_queues::main);
  ASSERT_FALSE(handle.done());

  ASSERT_TRUE(continue_f);
  continue_f(452);
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 452);
}

TEST(await_callback, callback_arg_string) {
  async_coro::unique_function<void(std::string)> continue_f;

  auto routine_1 = [&continue_f]() -> async_coro::task<std::string> {
    auto res = co_await async_coro::await_callback_with_result<std::string>(
        [&continue_f](auto f) { continue_f = std::move(f); });
    co_return std::move(res);
  };

  auto routine_2 = [routine_1]() -> async_coro::task<std::string> {
    co_return co_await routine_1();
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine_2, async_coro::execution_queues::main);
  ASSERT_FALSE(handle.done());

  ASSERT_TRUE(continue_f);
  continue_f("Looooooong StriiiinnnGgg!");
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), "Looooooong StriiiinnnGgg!");

  handle = scheduler.start_task(routine_2, async_coro::execution_queues::main);
  ASSERT_FALSE(handle.done());

  ASSERT_TRUE(continue_f);
  continue_f("Short String");
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), "Short String");
}
