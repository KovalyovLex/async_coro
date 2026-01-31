#include <async_coro/await/get_scheduler.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <gtest/gtest.h>

#include <thread>
#include <tuple>
#include <type_traits>
#include <variant>

namespace test_utils {

template <class T, template <class...> class Template>
struct is_specialization : std::false_type {};

template <template <class...> class Template, class... Args>
struct is_specialization<Template<Args...>, Template> : std::true_type {};

template <class T>
constexpr auto int_visitor_impl(T num);

template <class... TArgs>
constexpr auto int_applier_impl(TArgs... num) {
  return (int_visitor_impl(num) + ...);
}

template <class T>
constexpr auto int_visitor_impl(T num) {
  if constexpr (!std::is_same_v<T, std::monostate>) {
    if constexpr (is_specialization<T, std::tuple>::value) {
      return std::apply([](auto... nums) { return int_applier_impl(nums...); }, num);
    } else if constexpr (is_specialization<T, std::variant>::value) {
      return std::visit([](auto n) { return int_visitor_impl(n); }, num);
    } else {
      return int(num);
    }
  } else {
    return 0;
  }
}

constexpr auto int_visitor = [](auto num) {
  return int_visitor_impl(num);
};

}  // namespace test_utils

TEST(task_op, and_result_pair) {
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

TEST(task_op, and_result_triple_with_void) {
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

    {
      auto results = co_await (scheduler.start_task(routine2) && scheduler.start_task(routine1) && scheduler.start_task(routine3));
      static_assert(std::is_same_v<decltype(results), std::tuple<float, int>>);
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

TEST(task_op, and_result_with_parenthesis) {
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

      const auto sum = std::apply([&](auto... num) { return (int(num) + ...); }, results);
      EXPECT_EQ(sum, 6);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine_void) && (scheduler.start_task(routine2) && scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);

      const auto sum = std::apply([&](auto... num) { return (int(num) + ...); }, results);
      EXPECT_EQ(sum, 6);
    }
    {
      auto results = co_await ((scheduler.start_task(routine1) && scheduler.start_task(routine_void)) && (scheduler.start_task(routine2) && scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);

      const auto sum = std::apply([&](auto... num) { return (int(num) + ...); }, results);
      EXPECT_EQ(sum, 6);
    }

    auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine2) && scheduler.start_task(routine3) && scheduler.start_task(routine_void));

    static_assert(std::is_same_v<decltype(results), std::tuple<int, float, double>>);

    const auto sum = std::apply([&](auto... num) { return (int(num) + ...); }, results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 6);
}

TEST(task_op, or_result_pair) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();

    auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine2));

    static_assert(std::is_same_v<decltype(results), std::variant<int, float>>);

    co_return std::visit(
        [&](auto num) {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            return int(num);
          } else {
            return 0;
          }
        },
        results);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 1);
}

TEST(task_op, or_result_triple_with_void) {
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
      auto results = co_await (scheduler.start_task(routine3) || scheduler.start_task(routine1) || scheduler.start_task(routine2));
      static_assert(std::is_same_v<decltype(results), std::variant<std::monostate, int, float>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 0);
    }

    {
      auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine3) || scheduler.start_task(routine2));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::monostate, float>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }

    {
      auto results = co_await (scheduler.start_task(routine2) || scheduler.start_task(routine1) || scheduler.start_task(routine3));
      static_assert(std::is_same_v<decltype(results), std::variant<float, int, std::monostate>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 3);
    }

    auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine2) || scheduler.start_task(routine3));

    static_assert(std::is_same_v<decltype(results), std::variant<int, float, std::monostate>>);

    co_return std::visit(test_utils::int_visitor, results);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 1);
}

TEST(task_op, or_result_with_parenthesis) {
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
      auto results = co_await (scheduler.start_task(routine1) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)) || scheduler.start_task(routine_void));
      static_assert(std::is_same_v<decltype(results), std::variant<int, float, double, std::monostate>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine_void) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::monostate, float, double>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await ((scheduler.start_task(routine1) || scheduler.start_task(routine_void)) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::monostate, float, double>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }

    auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine2) || scheduler.start_task(routine3) || scheduler.start_task(routine_void));

    static_assert(std::is_same_v<decltype(results), std::variant<int, float, double, std::monostate>>);

    co_return std::visit(test_utils::int_visitor, results);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 1);
}

TEST(task_op, mixed_result_with_parenthesis) {
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

    ASYNC_CORO_WARNINGS_GCC_PUSH
    ASYNC_CORO_WARNINGS_GCC_IGNORE("parentheses")

    {
      auto results = co_await (scheduler.start_task(routine1) && (scheduler.start_task(routine2) || scheduler.start_task(routine3)) || scheduler.start_task(routine_void));
      static_assert(std::is_same_v<decltype(results), std::variant<std::tuple<int, std::variant<float, double>>, std::monostate>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 4);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)) && scheduler.start_task(routine_void));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::tuple<std::variant<float, double>>>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) && scheduler.start_task(routine_void) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<std::tuple<int>, float, double>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine_void) && (scheduler.start_task(routine2) || scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::tuple<std::variant<float, double>>>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await ((scheduler.start_task(routine1) && scheduler.start_task(routine_void)) || (scheduler.start_task(routine2) || scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<std::tuple<int>, float, double>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }
    {
      auto results = co_await ((scheduler.start_task(routine1) || scheduler.start_task(routine_void)) || (scheduler.start_task(routine2) && scheduler.start_task(routine3)));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::monostate, std::tuple<float, double>>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);
    }

    {
      auto results = co_await (scheduler.start_task(routine1) || scheduler.start_task(routine2) && scheduler.start_task(routine3) || scheduler.start_task(routine_void));
      static_assert(std::is_same_v<decltype(results), std::variant<int, std::tuple<float, double>, std::monostate>>);

      const auto res = std::visit(test_utils::int_visitor, results);
      EXPECT_EQ(res, 1);

      co_return res;
    }

    ASYNC_CORO_WARNINGS_GCC_POP
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 1);
}

TEST(task_op, or_operator_with_delayed_tasks) {
  auto slow_routine = []() -> async_coro::task<int> {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return 1;
  };

  auto fast_routine = []() -> async_coro::task<double> {
    co_return 2.0;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();

    // The || operator should complete as soon as fast_routine completes
    auto result = co_await (scheduler.start_task(slow_routine, async_coro::execution_queues::worker) || scheduler.start_task(fast_routine, async_coro::execution_queues::worker));

    // Should get fast_routine's result since it completes first
    EXPECT_TRUE(std::holds_alternative<double>(result));
    auto value = std::get<double>(result);
    EXPECT_DOUBLE_EQ(value, 2.0);
    co_return static_cast<int>(value);
  };

  async_coro::execution_system_config config{
      .worker_configs = {
          {"worker1", async_coro::execution_queues::worker},
          {"worker2", async_coro::execution_queues::worker}}};
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(config)};

  auto handle = scheduler.start_task(routine(), async_coro::execution_queues::main);

  int counter = 0;
  while (!handle.done() && counter++ < 1000000) {
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
    std::this_thread::yield();
  }
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 2);
}

TEST(task_op, mixed_void_and_value_operators) {
  auto void_routine = []() -> async_coro::task<> {
    co_return;
  };

  auto int_routine = []() -> async_coro::task<int> {
    co_return 42;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto& scheduler = co_await async_coro::get_scheduler();

    // Test void && int - void result should be filtered out
    auto result = co_await (scheduler.start_task(void_routine) && scheduler.start_task(int_routine));
    EXPECT_EQ(std::get<0>(result), 42);

    // Test int || void
    auto result2 = co_await (scheduler.start_task(int_routine) || scheduler.start_task(void_routine));
    EXPECT_TRUE(std::holds_alternative<int>(result2));
    EXPECT_EQ(std::get<int>(result2), 42);

    co_return 42;
  };

  async_coro::execution_system_config config{.worker_configs = {{"worker1", async_coro::execution_queues::worker}}};
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(config)};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 42);
}

TEST(task_op, different_types_and_operator) {
  auto int_routine = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto double_routine = []() -> async_coro::task<double> {
    co_return 2.5;
  };

  auto routine = [&]() -> async_coro::task<double> {
    auto& scheduler = co_await async_coro::get_scheduler();

    // Test combining different types with &&
    auto [int_val, double_val] = co_await (
        scheduler.start_task(int_routine) &&
        scheduler.start_task(double_routine));

    EXPECT_EQ(int_val, 1);
    EXPECT_DOUBLE_EQ(double_val, 2.5);

    co_return int_val + double_val;
  };

  async_coro::execution_system_config config{.worker_configs = {{"worker1", async_coro::execution_queues::worker}}};
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(config)};

  auto handle = scheduler.start_task(routine());
  ASSERT_TRUE(handle.done());
  EXPECT_DOUBLE_EQ(handle.get(), 3.5);
}
