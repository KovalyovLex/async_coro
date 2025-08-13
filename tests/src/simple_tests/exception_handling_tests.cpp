#include <async_coro/config.h>

#if ASYNC_CORO_WITH_EXCEPTIONS

#include <async_coro/await_callback.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/start_task.h>
#include <async_coro/switch_to_queue.h>
#include <async_coro/task.h>
#include <async_coro/task_launcher.h>
#include <async_coro/when_all.h>
#include <async_coro/when_any.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

namespace exception_handling_tests {

// Custom exception types for testing
struct test_exception : std::runtime_error {
  explicit test_exception(const std::string& message) : std::runtime_error(message) {}
};

struct nested_exception : std::runtime_error {
  explicit nested_exception(const std::string& message) : std::runtime_error(message) {}
};

// Helper class to track exception propagation
struct exception_tracker {
  static std::atomic<int> exception_count;
  static std::atomic<int> cleanup_count;

  static void reset() {
    exception_count = 0;
    cleanup_count = 0;
  }

  ~exception_tracker() {
    cleanup_count++;
  }
};

std::atomic<int> exception_tracker::exception_count{0};
std::atomic<int> exception_tracker::cleanup_count{0};

}  // namespace exception_handling_tests

using namespace exception_handling_tests;

// Simple exception tests
TEST(exception_handling, simple_throw_in_coroutine) {
  auto throwing_coroutine = []() -> async_coro::task<int> {
    throw test_exception("Simple exception test");
    co_return 42;  // This should never be reached
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(throwing_coroutine());

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const test_exception& e) {
    EXPECT_STREQ(e.what(), "Simple exception test");
  } catch (...) {
    FAIL() << "Expected test_exception, but got different exception";
  }
}

TEST(exception_handling, throw_after_await) {
  std::function<void()> continue_f;

  auto throwing_coroutine = [&continue_f]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) {
      continue_f = std::move(f);
    });

    throw test_exception("Exception after await");
    co_return 42;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(throwing_coroutine());

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);

  continue_f();

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const test_exception& e) {
    EXPECT_STREQ(e.what(), "Exception after await");
  }
}

TEST(exception_handling, throw_in_void_coroutine) {
  auto throwing_void_coroutine = []() -> async_coro::task<void> {
    throw test_exception("Void coroutine exception");
    co_return;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(throwing_void_coroutine());

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const test_exception& e) {
    EXPECT_STREQ(e.what(), "Void coroutine exception");
  }
}

// Embedded coroutine exception tests
TEST(exception_handling, nested_coroutine_throw) {
  auto inner_coroutine = []() -> async_coro::task<int> {
    throw test_exception("Inner coroutine exception");
    co_return 10;
  };

  auto outer_coroutine = [inner_coroutine]() -> async_coro::task<int> {
    try {
      auto result = co_await inner_coroutine();
      co_return result + 5;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Inner coroutine exception");
      throw nested_exception("Outer coroutine re-throw");
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(outer_coroutine());

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const nested_exception& e) {
    EXPECT_STREQ(e.what(), "Outer coroutine re-throw");
  }
}

TEST(exception_handling, nested_coroutine_catch_and_return) {
  auto inner_coroutine = []() -> async_coro::task<int> {
    throw test_exception("Inner coroutine exception");
    co_return 10;
  };

  auto outer_coroutine = [inner_coroutine]() -> async_coro::task<int> {
    try {
      auto result = co_await inner_coroutine();
      co_return result + 5;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Inner coroutine exception");
      co_return -1;  // Return error value instead of re-throwing
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(outer_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, deep_nested_exception_propagation) {
  auto level3_coroutine = []() -> async_coro::task<int> {
    throw test_exception("Level 3 exception");
    co_return 3;
  };

  auto level2_coroutine = [level3_coroutine]() -> async_coro::task<int> {
    auto result = co_await level3_coroutine();
    co_return result * 2;
  };

  auto level1_coroutine = [level2_coroutine]() -> async_coro::task<int> {
    auto result = co_await level2_coroutine();
    co_return result + 1;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(level1_coroutine());

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const test_exception& e) {
    EXPECT_STREQ(e.what(), "Level 3 exception");
  }
}

// when_all exception tests
TEST(exception_handling, when_all_one_task_throws) {
  auto normal_task = []() -> async_coro::task<int> {
    co_return 42;
  };

  auto throwing_task = []() -> async_coro::task<int> {
    throw test_exception("when_all throwing task");
    co_return 0;
  };

  auto main_coroutine = [normal_task, throwing_task]() -> async_coro::task<int> {
    try {
      auto results = co_await async_coro::when_all(
          async_coro::task_launcher{normal_task},
          async_coro::task_launcher{throwing_task});

      const auto sum = std::apply(
          [](auto... values) {
            return (int(values) + ...);
          },
          results);

      co_return sum;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "when_all throwing task");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, when_all_multiple_tasks_throw) {
  auto throwing_task1 = []() -> async_coro::task<int> {
    throw test_exception("First throwing task");
    co_return 1;
  };

  auto throwing_task2 = []() -> async_coro::task<int> {
    throw nested_exception("Second throwing task");
    co_return 2;
  };

  auto normal_task = []() -> async_coro::task<int> {
    co_return 42;
  };

  auto main_coroutine = [throwing_task1, throwing_task2, normal_task]() -> async_coro::task<int> {
    try {
      auto results = co_await async_coro::when_all(
          async_coro::task_launcher{throwing_task1},
          async_coro::task_launcher{throwing_task2},
          async_coro::task_launcher{normal_task});

      const auto sum = std::apply(
          [](auto... values) {
            return (int(values) + ...);
          },
          results);

      co_return sum;
    } catch (const std::exception& e) {
      // Should catch the first exception encountered
      EXPECT_TRUE(std::string(e.what()) == "First throwing task" ||
                  std::string(e.what()) == "Second throwing task");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, when_all_async_throwing_tasks) {
  std::function<void()> continue_f1;
  std::function<void()> continue_f2;

  auto async_throwing_task1 = [&continue_f1]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f1](auto f) {
      continue_f1 = std::move(f);
    });

    throw test_exception("Async throwing task 1");
    co_return 1;
  };

  auto async_throwing_task2 = [&continue_f2]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f2](auto f) {
      continue_f2 = std::move(f);
    });

    throw nested_exception("Async throwing task 2");
    co_return 2;
  };

  auto main_coroutine = [async_throwing_task1, async_throwing_task2]() -> async_coro::task<int> {
    try {
      auto results = co_await async_coro::when_all(
          async_coro::task_launcher{async_throwing_task1},
          async_coro::task_launcher{async_throwing_task2});

      const auto sum = std::apply(
          [](auto... values) {
            return (int(values) + ...);
          },
          results);

      co_return sum;
    } catch (const std::exception& e) {
      EXPECT_STREQ(e.what(), "Async throwing task 1");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f1);
  ASSERT_TRUE(continue_f2);

  // Resume both tasks to trigger exceptions
  continue_f1();
  continue_f2();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

// when_any exception tests
TEST(exception_handling, when_any_one_task_throws) {
  auto normal_task = []() -> async_coro::task<int> {
    co_return 42;
  };

  auto throwing_task = []() -> async_coro::task<int> {
    throw test_exception("when_any throwing task");
    co_return 0;
  };

  auto main_coroutine = [normal_task, throwing_task]() -> async_coro::task<int> {
    try {
      auto result = co_await async_coro::when_any(
          async_coro::task_launcher{normal_task},
          async_coro::task_launcher{throwing_task});

      int value = 0;
      std::visit([&value](auto v) {
        if constexpr (!std::is_same_v<decltype(v), std::monostate>) {
          value = int(v);
        }
      },
                 result);

      co_return value;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "when_any throwing task");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  // Should get the result from the normal task (42)
  EXPECT_EQ(handle.get(), 42);
}

TEST(exception_handling, when_any_all_tasks_throw) {
  auto throwing_task1 = []() -> async_coro::task<int> {
    throw test_exception("First throwing task");
    co_return 1;
  };

  auto throwing_task2 = []() -> async_coro::task<int> {
    throw nested_exception("Second throwing task");
    co_return 2;
  };

  auto main_coroutine = [throwing_task1, throwing_task2]() -> async_coro::task<int> {
    try {
      auto result = co_await async_coro::when_any(
          async_coro::task_launcher{throwing_task1},
          async_coro::task_launcher{throwing_task2});

      int value = 0;
      std::visit([&value](auto v) {
        if constexpr (!std::is_same_v<decltype(v), std::monostate>) {
          value = int(v);
        }
      },
                 result);

      co_return value;
    } catch (const std::exception& e) {
      EXPECT_STREQ(e.what(), "First throwing task");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

// start_task exception tests
TEST(exception_handling, start_task_throws) {
  auto throwing_task = []() -> async_coro::task<int> {
    throw test_exception("start_task exception");
    co_return 42;
  };

  auto main_coroutine = [throwing_task]() -> async_coro::task<int> {
    try {
      auto handle = co_await async_coro::start_task(async_coro::task_launcher{throwing_task});
      co_return handle.get();
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "start_task exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, start_task_nested_throws) {
  auto inner_throwing_task = []() -> async_coro::task<int> {
    throw test_exception("Inner start_task exception");
    co_return 10;
  };

  auto outer_task = [inner_throwing_task]() -> async_coro::task<int> {
    auto result = co_await async_coro::start_task(async_coro::task_launcher{inner_throwing_task});
    co_return result.get() * 2;
  };

  auto main_coroutine = [outer_task]() -> async_coro::task<int> {
    try {
      auto handle = co_await async_coro::start_task(async_coro::task_launcher{outer_task});
      co_return handle.get();
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Inner start_task exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

// await_callback exception tests
TEST(exception_handling, await_callback_throws_in_callback) {
  std::function<void()> continue_f;

  auto callback_throwing_coroutine = [&continue_f]() -> async_coro::task<int> {
    try {
      co_await async_coro::await_callback([&continue_f](auto f) {
        continue_f = std::move(f);
        throw test_exception("Callback exception");
      });

      co_return 42;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Callback exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(callback_throwing_coroutine());

  ASSERT_TRUE(handle.done());
  ASSERT_TRUE(continue_f);

  continue_f();

  // The coroutine should still complete with the caught exception result
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, await_callback_throws_after_resume) {
  std::function<void()> continue_f;

  auto callback_throwing_coroutine = [&continue_f]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) {
      continue_f = std::move(f);
    });

    throw test_exception("Exception after callback resume");
    co_return 42;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(callback_throwing_coroutine());

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);

  continue_f();

  ASSERT_TRUE(handle.done());

  try {
    handle.get();
    FAIL() << "Expected exception to be thrown";
  } catch (const test_exception& e) {
    EXPECT_STREQ(e.what(), "Exception after callback resume");
  }
}

// Complex scenarios with multiple exception sources
TEST(exception_handling, mixed_success_and_failure_scenarios) {
  std::function<void()> continue_f1;
  std::function<void()> continue_f2;

  auto successful_task = []() -> async_coro::task<int> {
    co_return 100;
  };

  auto async_throwing_task = [&continue_f1]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f1](auto f) {
      continue_f1 = std::move(f);
    });

    throw test_exception("Async task exception");
    co_return 200;
  };

  auto callback_throwing_task = [&continue_f2]() -> async_coro::task<int> {
    try {
      co_await async_coro::await_callback([&continue_f2](auto f) {
        continue_f2 = std::move(f);
        throw nested_exception("Callback exception");
      });

      co_return 300;
    } catch (const nested_exception& e) {
      EXPECT_STREQ(e.what(), "Callback exception");
      co_return -300;
    }
  };

  auto main_coroutine = [successful_task, async_throwing_task, callback_throwing_task]() -> async_coro::task<int> {
    try {
      auto results = co_await async_coro::when_all(
          async_coro::task_launcher{successful_task},
          async_coro::task_launcher{async_throwing_task},
          async_coro::task_launcher{callback_throwing_task});

      const auto sum = std::apply(
          [](auto... values) {
            return (int(values) + ...);
          },
          results);

      co_return sum;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Async task exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f1);
  ASSERT_TRUE(continue_f2);

  // Trigger the callback exception first
  continue_f2();

  // Then trigger the async task exception
  continue_f1();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

// Exception propagation with different execution queues
TEST(exception_handling, exception_propagation_across_queues) {
  auto worker_throwing_task = []() -> async_coro::task<int> {
    co_await async_coro::switch_to_queue(async_coro::execution_queues::worker);

    throw test_exception("Worker queue exception");
    co_return 42;
  };

  auto main_coroutine = [worker_throwing_task]() -> async_coro::task<int> {
    try {
      auto result = co_await worker_throwing_task();
      co_return result;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Worker queue exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{{{"worker1"}}})};

  auto handle = scheduler.start_task(main_coroutine());

  // Wait for worker thread to process
  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

// Exception with lifetime tracking
TEST(exception_handling, exception_with_lifetime_tracking) {
  exception_tracker::reset();

  auto throwing_task_with_tracker = []() -> async_coro::task<int> {
    exception_tracker tracker;
    throw test_exception("Exception with lifetime tracking");
    co_return 42;
  };

  auto main_coroutine = [throwing_task_with_tracker]() -> async_coro::task<int> {
    try {
      auto result = co_await throwing_task_with_tracker();
      co_return result;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Exception with lifetime tracking");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);

  // Verify cleanup occurred
  EXPECT_GT(exception_tracker::cleanup_count, 0);
}

// Exception in void coroutines with when_all/when_any
TEST(exception_handling, void_coroutines_with_exceptions) {
  auto void_throwing_task = []() -> async_coro::task<void> {
    throw test_exception("Void task exception");
    co_return;
  };

  auto void_normal_task = []() -> async_coro::task<void> {
    co_return;
  };

  auto main_coroutine = [void_normal_task, void_throwing_task]() -> async_coro::task<int> {
    try {
      co_await async_coro::when_all(
          async_coro::task_launcher{void_normal_task},
          async_coro::task_launcher{void_throwing_task});

      co_return 42;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Void task exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), -1);
}

TEST(exception_handling, void_coroutines_when_any_exception) {
  auto void_throwing_task = []() -> async_coro::task<void> {
    throw test_exception("Void when_any exception");
    co_return;
  };

  auto void_normal_task = []() -> async_coro::task<void> {
    co_return;
  };

  auto main_coroutine = [void_normal_task, void_throwing_task]() -> async_coro::task<int> {
    try {
      auto result = co_await async_coro::when_any(
          async_coro::task_launcher{void_normal_task},
          async_coro::task_launcher{void_throwing_task});

      EXPECT_EQ(result.index(), 0);  // Should get the normal task
      co_return 42;
    } catch (const test_exception& e) {
      EXPECT_STREQ(e.what(), "Void when_any exception");
      co_return -1;
    }
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(main_coroutine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 42);  // Should succeed with normal task
}

#endif
