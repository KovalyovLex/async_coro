#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task_handle_operators.h>
#include <gtest/gtest.h>

#include <type_traits>

TEST(task_op_and, result_pair) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();

    auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine2));

    static_assert(std::is_same_v<decltype(results), std::tuple<int, float>>);

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 4);
}

TEST(task_op_and, result_triple_with_void) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine3 = []() -> async_coro::task<> {
    co_return;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();
    {
      auto results = co_await (scheduler.start_task(routine3) && scheduler.start_task(routine1) && scheduler.start_task(routine2));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float>>);
    }

    {
      auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine3) && scheduler.start_task(routine2));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float>>);
    }

    auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine2) && scheduler.start_task(routine3));

    static_assert(std::is_same_v<decltype(results), std::tuple<int, float>>);

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 4);
}

TEST(task_op_and, result_with_parenthesis) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine3 = []() -> async_coro::task<double> {
    co_return 2.73;
  };

  auto routine_void = []() -> async_coro::task<> {
    co_return;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();

    {
      auto results = co_await (scheduler.start_task(routine1) && (scheduler.start_task(routine2) && scheduler.start_task(routine3)) && scheduler.start_task(routine_void));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine_void) && (scheduler.start_task(routine2) && scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);
    }
    {
      auto results = co_await ((scheduler.start_task(routine1) && scheduler.start_task(routine_void)) && (scheduler.start_task(routine2) && scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);
    }

    auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine2) && scheduler.start_task(routine3) && scheduler.start_task(routine_void));

    static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 6);
}