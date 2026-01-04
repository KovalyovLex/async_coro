#include <async_coro/await/await_callback.h>
#include <async_coro/await/start_task.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <async_coro/task_launcher.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <variant>

namespace lambda_lifetime_tests {

// Helper class to track object lifetime
struct lifetime_tracker {
  static int num_instances;
  static int num_constructors;
  static int num_copy_constructors;
  static int num_move_constructors;
  static int num_destructors;

  int value;

  explicit lifetime_tracker(int v = 0) : value(v) {
    num_instances++;
    num_constructors++;
  }

  lifetime_tracker(const lifetime_tracker& other) : value(other.value) {
    num_instances++;
    num_copy_constructors++;
  }

  lifetime_tracker(lifetime_tracker&& other) noexcept : value(other.value) {
    num_instances++;
    num_move_constructors++;
    other.value = -1;  // Mark as moved
  }

  ~lifetime_tracker() {
    num_instances--;
    num_destructors++;
  }

  lifetime_tracker& operator=(const lifetime_tracker& other) {
    if (this != &other) {
      value = other.value;
    }
    return *this;
  }

  lifetime_tracker& operator=(lifetime_tracker&& other) noexcept {
    if (this != &other) {
      value = other.value;
      other.value = -1;  // Mark as moved
    }
    return *this;
  }

  static void reset() {
    num_instances = 0;
    num_constructors = 0;
    num_copy_constructors = 0;
    num_move_constructors = 0;
    num_destructors = 0;
  }
};

int lifetime_tracker::num_instances = 0;
int lifetime_tracker::num_constructors = 0;
int lifetime_tracker::num_copy_constructors = 0;
int lifetime_tracker::num_move_constructors = 0;
int lifetime_tracker::num_destructors = 0;

}  // namespace lambda_lifetime_tests

using namespace lambda_lifetime_tests;

TEST(lambda_lifetime, start_task_with_scheduler) {
  lifetime_tracker::reset();

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  async_coro::task_handle<int> handle;

  async_coro::unique_function<void()> continue_f;

  {
    // Create task with lambda that captures the argument
    auto task = [&continue_f, captured_arg = lifetime_tracker(42)]() -> async_coro::task<int> {
      // The captured argument should still be alive here
      EXPECT_EQ(lifetime_tracker::num_instances, 2);  // 1 from capture + 1 from temp lambda
      EXPECT_EQ(captured_arg.value, 42);

      co_await async_coro::await_callback([&continue_f](auto f) {
        continue_f = std::move(f);
      });

      // Captured argument should still be alive after async work
      EXPECT_EQ(lifetime_tracker::num_instances, 1);
      EXPECT_EQ(captured_arg.value, 42);

      co_return captured_arg.value;
    };

    EXPECT_EQ(lifetime_tracker::num_instances, 1);  // 1 captured in lambda

    // Start task with scheduler
    handle = scheduler.start_task(async_coro::task_launcher{std::move(task)});
  }

  EXPECT_EQ(lifetime_tracker::num_destructors, 1);  // temp lambda
  EXPECT_EQ(lifetime_tracker::num_instances, 1);    // 1 captured in lambda

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);

  continue_f();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(lifetime_tracker::num_instances, 1);  // 1 captured in lambda
  EXPECT_EQ(handle.get(), 42);

  handle = {};
  continue_f = {};

  // After task completion, captured argument should be destroyed
  EXPECT_EQ(lifetime_tracker::num_instances, 0);
  EXPECT_EQ(lifetime_tracker::num_destructors, 2);  // Original + captured copy
}

TEST(lambda_lifetime, start_task_in_coroutine) {
  lifetime_tracker::reset();

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  async_coro::task_handle<int> handle;
  async_coro::unique_function<void()> continue_f;

  {
    lifetime_tracker captured_arg(100);
    EXPECT_EQ(lifetime_tracker::num_instances, 1);

    // Create a coroutine that uses async_coro::start_task
    auto main_coroutine = [&continue_f, captured_arg = std::move(captured_arg)]() -> async_coro::task<int> {
      EXPECT_EQ(lifetime_tracker::num_instances, 3);
      EXPECT_EQ(captured_arg.value, 100);

      // Create nested task using async_coro::start_task
      auto nested_task = [&continue_f, captured_arg_ref = std::ref(captured_arg)]() -> async_coro::task<int> {
        // Captured argument should still be alive
        EXPECT_EQ(lifetime_tracker::num_instances, 3);
        EXPECT_EQ(captured_arg_ref.get().value, 100);

        co_await async_coro::await_callback([&continue_f](auto f) {
          continue_f = std::move(f);
        });

        // all temp objects should be destroyed
        EXPECT_EQ(lifetime_tracker::num_instances, 1);

        co_return captured_arg_ref.get().value;
      };

      // Start nested task using async_coro::start_task
      auto nested_handle = co_await async_coro::start_task(async_coro::task_launcher{std::move(nested_task)});

      co_await async_coro::await_callback([&nested_handle](auto f) {
        nested_handle.continue_with([f = std::move(f)](auto&, bool) mutable { f(); });
      });

      co_return captured_arg.value;
    };

    handle = scheduler.start_task(std::move(main_coroutine));
  }

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);

  continue_f();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 100);
  EXPECT_EQ(lifetime_tracker::num_instances, 1);  // coroutine still holds lambda as handle live

  handle = {};

  EXPECT_EQ(lifetime_tracker::num_instances, 0);
}

TEST(lambda_lifetime, when_all_tasks) {
  lifetime_tracker::reset();

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  async_coro::task_handle<int> handle;
  async_coro::unique_function<void()> continue_f1;
  async_coro::unique_function<void()> continue_f2;
  async_coro::unique_function<void()> continue_f3;

  {
    lifetime_tracker captured_arg1(10);
    lifetime_tracker captured_arg2(20);
    lifetime_tracker captured_arg3(30);

    EXPECT_EQ(lifetime_tracker::num_instances, 3);

    auto main_coroutine = [&continue_f1, &continue_f2, &continue_f3,
                           captured_arg1 = std::move(captured_arg1),
                           captured_arg2 = std::move(captured_arg2),
                           captured_arg3 = std::move(captured_arg3)]() -> async_coro::task<int> {
      EXPECT_EQ(lifetime_tracker::num_instances, 9);  // 3 original + 3 captured + 3 temp copies

      // Create multiple tasks with captured arguments
      auto task1 = [&continue_f1, captured_arg = std::ref(captured_arg1)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f1](auto f) {
          continue_f1 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      auto task2 = [&continue_f2, captured_arg = std::ref(captured_arg2)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f2](auto f) {
          continue_f2 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      auto task3 = [&continue_f3, captured_arg = std::ref(captured_arg3)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f3](auto f) {
          continue_f3 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      // Use when_all to wait for all tasks
      auto results = co_await (
          co_await async_coro::start_task(std::move(task1)) &&
          co_await async_coro::start_task(std::move(task2)) &&
          co_await async_coro::start_task(std::move(task3)));

      EXPECT_EQ(lifetime_tracker::num_instances, 3);

      // Sum up the results
      const auto sum = std::apply(
          [](auto... values) {
            return (int(values) + ...);
          },
          results);

      co_return sum;
    };

    EXPECT_EQ(lifetime_tracker::num_instances, 6);

    handle = scheduler.start_task(std::move(main_coroutine));

    EXPECT_EQ(lifetime_tracker::num_instances, 9);  // temp main_coroutine still alive
  }

  // Captured arguments should still be alive
  EXPECT_EQ(lifetime_tracker::num_instances, 3);

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f1);
  ASSERT_TRUE(continue_f2);
  ASSERT_TRUE(continue_f3);

  continue_f1();
  continue_f2();
  continue_f3();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(lifetime_tracker::num_instances, 3);

  EXPECT_EQ(handle.get(), 60);  // 10 + 20 + 30

  handle = {};

  EXPECT_EQ(lifetime_tracker::num_instances, 0);
}

TEST(lambda_lifetime, when_any_tasks) {
  lifetime_tracker::reset();

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  async_coro::task_handle<int> handle;
  async_coro::unique_function<void()> continue_f1;
  async_coro::unique_function<void()> continue_f2;
  async_coro::unique_function<void()> continue_f3;

  {
    lifetime_tracker captured_arg1(100);
    lifetime_tracker captured_arg2(200);
    lifetime_tracker captured_arg3(300);

    EXPECT_EQ(lifetime_tracker::num_instances, 3);

    auto main_coroutine = [&continue_f1, &continue_f2, &continue_f3,
                           captured_arg1 = std::move(captured_arg1),
                           captured_arg2 = std::move(captured_arg2),
                           captured_arg3 = std::move(captured_arg3)]() -> async_coro::task<int> {
      EXPECT_EQ(lifetime_tracker::num_instances, 9);

      // Create tasks with different completion times
      auto task1 = [&continue_f1, captured_arg = std::ref(captured_arg1)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f1](auto f) {
          continue_f1 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      auto task2 = [&continue_f2, captured_arg = std::ref(captured_arg2)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f2](auto f) {
          continue_f2 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      auto task3 = [&continue_f3, captured_arg = std::ref(captured_arg3)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 9);
        co_await async_coro::await_callback([&continue_f3](auto f) {
          continue_f3 = std::move(f);
        });
        co_return captured_arg.get().value;
      };

      // Use when_any to wait for the first task to complete
      auto result = co_await (
          co_await async_coro::start_task(std::move(task1)) ||
          co_await async_coro::start_task(std::move(task2)) ||
          co_await async_coro::start_task(std::move(task3)));

      EXPECT_EQ(lifetime_tracker::num_instances, 3);

      // Extract the result from the variant
      int sum = 0;
      std::visit([&sum](auto value) {
        if constexpr (!std::is_same_v<decltype(value), std::monostate>) {
          sum = int(value);
        }
      },
                 result);

      co_return sum;
    };

    EXPECT_EQ(lifetime_tracker::num_instances, 6);

    handle = scheduler.start_task(std::move(main_coroutine));

    EXPECT_EQ(lifetime_tracker::num_instances, 9);
  }

  // Captured arguments should still be alive
  EXPECT_EQ(lifetime_tracker::num_instances, 3);

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f1);
  ASSERT_TRUE(continue_f2);
  ASSERT_TRUE(continue_f3);

  continue_f2();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(lifetime_tracker::num_instances, 3);

  // Should get the result from the fastest task (task2 with value 200)
  EXPECT_EQ(handle.get(), 200);

  continue_f1();
  continue_f3();

  handle = {};

  EXPECT_EQ(lifetime_tracker::num_instances, 0);
}

TEST(lambda_lifetime, embedded_coro) {
  lifetime_tracker::reset();

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  {
    EXPECT_EQ(lifetime_tracker::num_instances, 0);

    auto main_coroutine = []() mutable -> async_coro::task<int> {
      EXPECT_EQ(lifetime_tracker::num_instances, 0);

      {
        // Create tasks with different completion times
        auto task1 = [captured_arg = lifetime_tracker(100)]() -> async_coro::task<int> {
          EXPECT_EQ(lifetime_tracker::num_instances, 1);

          auto copy = captured_arg;
          EXPECT_EQ(lifetime_tracker::num_instances, 2);

          co_return copy.value;
        };
        EXPECT_EQ(lifetime_tracker::num_instances, 1);  // lambda captured

        auto result = co_await task1();
        EXPECT_EQ(result, 100);
        EXPECT_EQ(lifetime_tracker::num_instances, 1);  // only lambda capture
      }

      EXPECT_EQ(lifetime_tracker::num_instances, 0);

      {
        auto task2 = [captured_arg = lifetime_tracker(200)]() mutable -> async_coro::task<int> {
          EXPECT_EQ(lifetime_tracker::num_instances, 1);

          auto copy = std::move(captured_arg);
          EXPECT_EQ(lifetime_tracker::num_instances, 2);

          co_return copy.value;
        };
        EXPECT_EQ(lifetime_tracker::num_instances, 1);  // lambda captured

        auto result = co_await task2();
        EXPECT_EQ(result, 200);
        EXPECT_EQ(lifetime_tracker::num_instances, 1);  // only lambda capture
      }

      EXPECT_EQ(lifetime_tracker::num_instances, 0);

      auto task3 = [captured_arg = lifetime_tracker(300)]() -> async_coro::task<int> {
        EXPECT_EQ(lifetime_tracker::num_instances, 1);
        co_return captured_arg.value;
      };
      EXPECT_EQ(lifetime_tracker::num_instances, 1);

      auto result = co_await task3();
      EXPECT_EQ(result, 300);
      EXPECT_EQ(lifetime_tracker::num_instances, 1);

      co_return result;
    };

    EXPECT_EQ(lifetime_tracker::num_instances, 0);

    auto handle = scheduler.start_task(std::move(main_coroutine));

    EXPECT_EQ(lifetime_tracker::num_instances, 0);
    EXPECT_TRUE(handle.done());
  }

  EXPECT_EQ(lifetime_tracker::num_instances, 0);
}
