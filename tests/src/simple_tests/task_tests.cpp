#include <async_coro/await_callback.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/start_task.h>
#include <async_coro/switch_to_queue.h>
#include <async_coro/task.h>
#include <async_coro/task_launcher.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <numbers>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <variant>

namespace task_tests {
struct coro_runner {
  template <typename T>
  void run_coroutine(async_coro::task<T> coro) {
    _scheduler.start_task(std::move(coro));
  }

  async_coro::scheduler _scheduler;
};

struct release_sema_in_destructor {
  explicit release_sema_in_destructor(std::binary_semaphore& ref) noexcept : sema_ref(ref) {}
  ~release_sema_in_destructor() {
    sema_ref.release();
  }
  std::binary_semaphore& sema_ref;  // NOLINT(*-ref-*)
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
}

TEST(task, resume_on_callback_deep) {
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
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());
  ASSERT_TRUE(continue_f);
  continue_f();
  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 45);
}

TEST(task, resume_on_callback) {
  async_coro::unique_function<void()> continue_f;

  auto routine = [](auto& cnt) -> async_coro::task<int> {  // NOLINT(*-reference-coroutine-*)
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

TEST(task, async_execution) {
  static std::atomic_bool async_done = false;

  auto routine_1 = []() -> async_coro::task<float> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_to_queue(async_coro::execution_queues::worker);

    EXPECT_NE(current_th, std::this_thread::get_id());

    async_done = true;

    co_await switch_to_queue(async_coro::execution_queues::main);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return 2.34f;
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto current_th = std::this_thread::get_id();

    auto res = co_await start();

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return (int) res;
  }(routine_1);

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"},
                                                             {"worker2"}}})};

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());

  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  ASSERT_FALSE(handle.done());

  if (!async_done) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  ASSERT_TRUE(async_done);

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 2);
}

TEST(task, async_no_switch) {
  static std::atomic_bool async_done = false;

  auto routine_1 = []() -> async_coro::task<float> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_to_queue(async_coro::execution_queues::worker);

    EXPECT_NE(current_th, std::this_thread::get_id());

    co_await switch_to_queue(async_coro::execution_queues::worker);

    async_done = true;

    co_await switch_to_queue(async_coro::execution_queues::main);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_await switch_to_queue(async_coro::execution_queues::main);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return 7.14f;
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_to_queue(async_coro::execution_queues::main);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    auto res = co_await start();

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return (int) res;
  }(routine_1);

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  ASSERT_FALSE(routine.done());
  auto handle = scheduler.start_task(std::move(routine));
  ASSERT_FALSE(handle.done());

  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  ASSERT_FALSE(handle.done());

  if (!async_done) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  ASSERT_TRUE(async_done);

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), 7);
}

TEST(task, when_all) {
  std::binary_semaphore sema{0};

  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine3 = [&]() -> async_coro::task<double> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    sema.release();

    co_return 2.72;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await (
        co_await async_coro::start_task(routine1) &&
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine3, async_coro::execution_queues::worker));

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_FALSE(handle.done());
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 6);
}

TEST(task, when_all_void) {
  std::binary_semaphore sema{0};

  auto routine1 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine2 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine3 = [&]() -> async_coro::task<void> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    sema.release();

    co_return;
  };

  bool executed = false;
  auto routine = [&]() -> async_coro::task<void> {
    co_await (
        co_await async_coro::start_task(routine1, async_coro::execution_queues::worker) &&
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine3, async_coro::execution_queues::worker));

    executed = true;
    co_return;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  EXPECT_FALSE(executed);

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_FALSE(executed);

  EXPECT_FALSE(handle.done());
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
}

TEST(task, when_all_with_void) {
  std::binary_semaphore sema{0};

  auto routine1 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine2 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine3 = [&]() -> async_coro::task<float> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    sema.release();

    co_return 3.14f;
  };

  bool executed = false;
  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await (
        co_await async_coro::start_task(routine1(), async_coro::execution_queues::worker) &&
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine3, async_coro::execution_queues::worker));

    executed = true;
    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  EXPECT_FALSE(executed);

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_FALSE(executed);

  EXPECT_FALSE(handle.done());
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 5);
}

TEST(task, when_all_with_void_first) {
  std::binary_semaphore sema{0};

  auto routine1 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine2 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine3 = [&]() -> async_coro::task<float> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    sema.release();

    co_return 3.14f;
  };

  bool executed = false;
  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await (
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine1(), async_coro::execution_queues::worker) &&
        co_await async_coro::start_task(routine3(), async_coro::execution_queues::worker));

    executed = true;
    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  EXPECT_FALSE(executed);

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_FALSE(executed);

  EXPECT_FALSE(handle.done());
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 5);
}

TEST(task, when_all_with_void_last) {
  std::binary_semaphore sema{0};

  auto routine1 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine2 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine3 = [&]() -> async_coro::task<float> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    sema.release();

    co_return 3.14f;
  };

  bool executed = false;
  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await (
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine3(), async_coro::execution_queues::worker) &&
        co_await async_coro::start_task(routine1(), async_coro::execution_queues::worker));

    executed = true;
    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  EXPECT_FALSE(executed);

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_FALSE(executed);

  EXPECT_FALSE(handle.done());
  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 5);
}

TEST(task, when_all_no_wait) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await (
        co_await async_coro::start_task(routine1) &&
        co_await async_coro::start_task(routine2));

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 5);
}

TEST(task, when_any_no_wait_sleep) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine3 = [&]() -> async_coro::task<double> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    co_return 2.72;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine3(), async_coro::execution_queues::worker));

    int sum = 0;
    std::visit([&](auto num) { return sum = int(num); }, result);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  std::this_thread::sleep_for(std::chrono::milliseconds{1});

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 1);
}

TEST(task, when_any_no_wait) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = [&]() -> async_coro::task<double> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return 2.72;
  };

  auto routine3 = []() -> async_coro::task<int> {
    co_return 3;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine3));

    int sum = 0;
    std::visit([&](auto num) { return sum = int(num); }, result);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 1);
}

TEST(task, when_any_void_all) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine2 = [&]() -> async_coro::task<void> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return;
  };

  auto routine = [&]() -> async_coro::task<void> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2));

    EXPECT_EQ(result.index(), 0);

    co_return;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
}

TEST(task, when_any_void_first) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine2 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return 2;
  };

  auto routine3 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine3));

    EXPECT_EQ(result.index(), 0);

    co_return std::visit(
        [sum = 0](auto num) mutable {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            sum = int(num);
          }
          return sum;
        },
        result);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 0);
}

TEST(task, when_any_void_last) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine2 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return 2;
  };

  auto routine3 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine3) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine1));

    EXPECT_EQ(result.index(), 0);

    co_return std::visit(
        [sum = 0](auto num) mutable {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            sum = int(num);
          }
          return sum;
        },
        result);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, when_any_void_mid) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = []() -> async_coro::task<void> {
    co_return;
  };

  auto routine2 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return 2;
  };

  auto routine3 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine3) ||
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2));

    EXPECT_EQ(result.index(), 0);

    co_return std::visit(
        [sum = 0](auto num) mutable {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            sum = int(num);
          }
          return sum;
        },
        result);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, when_any_index_check) {
  async_coro::unique_function<void()> continue_f;

  auto routine1 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f](auto f) { continue_f = std::move(f); });

    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine3 = []() -> async_coro::task<int> {
    co_return 3;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine3));

    EXPECT_EQ(result.index(), 1);

    co_return std::visit(
        [sum = 0](auto num) mutable {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            sum = int(num);
          }
          return sum;
        },
        result);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f);

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  continue_f();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 2);
}

TEST(task, when_any_index_check_last) {
  async_coro::unique_function<void()> continue_f1;
  async_coro::unique_function<void()> continue_f2;

  auto routine1 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f1](auto f) { continue_f1 = std::move(f); });

    co_return 1;
  };

  auto routine2 = [&]() -> async_coro::task<int> {
    co_await async_coro::await_callback([&continue_f2](auto f) { continue_f2 = std::move(f); });

    co_return 2;
  };

  auto routine3 = []() -> async_coro::task<int> {
    co_return 3;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine3));

    EXPECT_EQ(result.index(), 2);

    co_return std::visit(
        [sum = 0](auto num) mutable {
          if constexpr (!std::is_same_v<decltype(num), std::monostate>) {
            sum = int(num);
          }
          return sum;
        },
        result);
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine());
  EXPECT_TRUE(handle.done());

  ASSERT_TRUE(continue_f1);
  ASSERT_TRUE(continue_f2);

  continue_f1();
  continue_f2();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, when_any_continue_after_parent_complete) {
  async_coro::unique_function<void()> continue_f1;
  async_coro::unique_function<void()> continue_f2;

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  {
    auto routine1 = [&]() -> async_coro::task<int> {
      co_await async_coro::await_callback([&continue_f1](auto f) { continue_f1 = std::move(f); });

      co_return 1;
    };

    auto routine2 = [&]() -> async_coro::task<int> {
      co_await async_coro::await_callback([&continue_f2](auto f) { continue_f2 = std::move(f); });

      co_return 2;
    };

    auto routine3 = []() -> async_coro::task<int> {
      co_return 3;
    };

    auto routine = [&]() -> async_coro::task<int> {
      auto result = co_await (
          co_await async_coro::start_task(routine1) ||
          co_await async_coro::start_task(routine2) ||
          co_await async_coro::start_task(routine3));

      EXPECT_EQ(result.index(), 2);

      co_return std::visit(
          [sum = 0](auto num) mutable {
            sum = int(num);
            return sum;
          },
          result);
    };

    auto handle = scheduler.start_task(routine());
    EXPECT_TRUE(handle.done());

    ASSERT_TRUE(continue_f1);
    ASSERT_TRUE(continue_f2);

    ASSERT_TRUE(handle.done());
    EXPECT_EQ(handle.get(), 3);

    continue_f1();
  }

  continue_f2();
}

TEST(task, when_any) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 1;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine3 = [&]() -> async_coro::task<double> {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    co_return 5.72;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto result = co_await (
        co_await async_coro::start_task(routine1(), async_coro::execution_queues::worker) ||
        co_await async_coro::start_task(routine2(), async_coro::execution_queues::worker) ||
        co_await async_coro::start_task(routine3(), async_coro::execution_queues::worker));

    int sum = 0;
    std::visit([&](auto num) { return sum = int(num); }, result);
    co_return sum;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}, {"worker2"}}})};

  auto handle = scheduler.start_task(routine());

  // wait for worker thread finish coro
  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

  ASSERT_TRUE(handle.done());
  EXPECT_LT(handle.get(), 4);
}

TEST(task, when_any_immediate_execution_order) {
  int index = 0;

  auto routine1 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 0);
    index = 1;
    co_return;
  };

  auto routine2 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 1);
    index = 2;
    co_return;
  };

  auto routine3 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 2);
    index = 3;
    co_return;
  };

  auto routine = [&]() -> async_coro::task<int> {
    co_await (
        co_await async_coro::start_task(routine1) ||
        co_await async_coro::start_task(routine2) ||
        co_await async_coro::start_task(routine3));

    EXPECT_EQ(index, 3);
    co_return index;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine);

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, when_all_immediate_execution_order) {
  int index = 0;

  auto routine1 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 0);
    index = 1;
    co_return;
  };

  auto routine2 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 1);
    index = 2;
    co_return;
  };

  auto routine3 = [&index]() -> async_coro::task<void> {
    EXPECT_EQ(index, 2);
    index = 3;
    co_return;
  };

  auto routine = [&]() -> async_coro::task<int> {
    co_await (
        co_await async_coro::start_task(routine1) &&
        co_await async_coro::start_task(routine2) &&
        co_await async_coro::start_task(routine3));

    EXPECT_EQ(index, 3);
    co_return index;
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  auto handle = scheduler.start_task(routine);

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 3);
}

TEST(task, task_handle_outlive) {
  static int num_instances = 0;
  num_instances = 0;

  struct destructible {
    destructible() { num_instances++; }
    destructible(const destructible& /*unused*/) { num_instances++; }
    ~destructible() { num_instances--; }
  };

  auto routine1 = []() -> async_coro::task<destructible> {
    co_return destructible{};
  };

  async_coro::scheduler scheduler;

  EXPECT_EQ(num_instances, 0);

  {
    auto handle = scheduler.start_task(routine1());

    ASSERT_TRUE(handle.done());

    EXPECT_EQ(num_instances, 1);
  }

  EXPECT_EQ(num_instances, 0);
}

TEST(task, task_handle_move_to_thread) {
  static int num_instances = 0;
  num_instances = 0;

  struct destructible {
    destructible() { num_instances++; }
    destructible(const destructible& /*unused*/) { num_instances++; }
    ~destructible() { num_instances--; }
  };

  std::atomic_bool ready = false;

  auto routine1 = [&]() -> async_coro::task<destructible> {
    co_await switch_to_queue(async_coro::execution_queues::worker);
    ready = true;
    co_await switch_to_queue(async_coro::execution_queues::main);

    co_return destructible{};
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"}}})};

  EXPECT_EQ(num_instances, 0);

  {
    auto handle = scheduler.start_task(routine1());

    std::thread th([handle2 = std::move(handle), &ready]() {
      while (!ready) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });

    ASSERT_TRUE(handle.done());  // NOLINT(*-move*)

    while (!ready) {
      std::this_thread::yield();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{1});

    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();

    th.join();

    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
  }

  EXPECT_EQ(num_instances, 0);
}

TEST(task, task_ref_result) {
  static int num_instances = 0;
  num_instances = 0;

  auto routine1 = []() -> async_coro::task<int&> {
    co_return num_instances;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine1());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(&handle.get(), &num_instances);
}

TEST(task, task_const_ref_result) {
  static int num_instances = 0;
  num_instances = 0;

  auto routine1 = []() -> async_coro::task<const int&> {
    co_return num_instances;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine1());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(&handle.get(), &num_instances);
}

TEST(task, task_ptr_result) {
  static int num_instances = 0;
  num_instances = 0;

  auto routine1 = []() -> async_coro::task<const int*> {
    co_return &num_instances;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine1());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), &num_instances);
}

TEST(task, task_ref_result_await) {
  static int num_instances = 0;
  num_instances = 0;

  auto routine1 = []() -> async_coro::task<int&> {
    co_return num_instances;
  };

  auto routine2 = [&]() -> async_coro::task<int&> {
    auto& res = co_await routine1();
    co_return res;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine2());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(&handle.get(), &num_instances);
}

TEST(task, task_const_ref_result_await) {
  static int num_instances = 0;
  num_instances = 0;

  auto routine1 = []() -> async_coro::task<const int&> {
    co_return num_instances;
  };

  auto routine2 = [&]() -> async_coro::task<const int&> {
    const auto& res = co_await routine1();
    co_return res;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine2());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(&handle.get(), &num_instances);
}

TEST(task, lambda_lifetime) {
  static int num_instances = 0;
  num_instances = 0;

  struct destructible {
    destructible() { num_instances++; }
    destructible(const destructible& /*unused*/) { num_instances++; }
    destructible(destructible&& /*unused*/) noexcept { num_instances++; }
    ~destructible() { num_instances--; }
  };

  async_coro::scheduler scheduler;
  async_coro::unique_function<void()> continue_f1;
  async_coro::unique_function<void()> continue_f2;

  async_coro::task_handle<void> handle;

  {
    destructible captured_arg;
    EXPECT_EQ(num_instances, 1);

    handle = scheduler.start_task(
        [&, captured_arg = std::move(captured_arg)]() noexcept -> async_coro::task<void> {
          EXPECT_LE(num_instances, 3);  // 2 or 3 because of no copy elision in captured arguments
          {
            destructible local_val;
            EXPECT_LE(num_instances, 4);
            co_await async_coro::await_callback(
                [&continue_f1](auto f) { continue_f1 = std::move(f); });
            EXPECT_EQ(num_instances, 2);
          }
          EXPECT_EQ(num_instances, 1);
          co_await async_coro::await_callback(
              [&continue_f2](auto f) { continue_f2 = std::move(f); });
          EXPECT_EQ(num_instances, 1);
        });
  }

  EXPECT_EQ(num_instances, 2);
  ASSERT_TRUE(continue_f1);
  EXPECT_FALSE(handle.done());

  continue_f1();
  EXPECT_EQ(num_instances, 1);
  ASSERT_TRUE(continue_f2);
  EXPECT_FALSE(handle.done());

  continue_f2();
  EXPECT_TRUE(handle.done());
  EXPECT_EQ(num_instances, 1);

  handle = {};
  EXPECT_EQ(num_instances, 0);
}

TEST(task, multiple_workers_async_execution) {
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{
          .worker_configs = {{"worker1", async_coro::execution_queues::worker},
                             {"worker2", async_coro::execution_queues::worker},
                             {"worker3", async_coro::execution_queues::worker},
                             {"worker4", async_coro::execution_queues::worker},
                             {"worker5", async_coro::execution_queues::worker}},
          .main_thread_allowed_tasks = async_coro::execution_queues::main})};

  auto routine = []() -> async_coro::task<float> {
    auto handle_void = co_await async_coro::start_task(
        []() -> async_coro::task<void> {
          co_return;
        },
        async_coro::execution_queues::worker);

    co_await std::move(handle_void);

    auto handle_int = co_await async_coro::start_task(
        []() -> async_coro::task<int> {
          co_return 3;
        },
        async_coro::execution_queues::worker);

    auto handle_float = co_await async_coro::start_task(
        []() -> async_coro::task<float> {
          co_return 2.34f;
        },
        async_coro::execution_queues::worker);

    auto handle_double = co_await async_coro::start_task(
        []() -> async_coro::task<double> {
          co_return std::numbers::pi;
        },
        async_coro::execution_queues::main);

    const auto res_int = co_await std::move(handle_int);
    EXPECT_EQ(res_int, 3);

    const auto res_double = co_await std::move(handle_double);
    EXPECT_DOUBLE_EQ(res_double, std::numbers::pi);

    const auto res_float = co_await std::move(handle_float);
    EXPECT_FLOAT_EQ(res_float, 2.34f);

    co_return res_float;
  };

  auto handle = scheduler.start_task(routine, async_coro::execution_queues::worker);
  ASSERT_FALSE(handle.done());

  std::size_t num_repeats = 0;
  while (!handle.done() && num_repeats++ < 1000000) {
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
    std::this_thread::yield();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_FLOAT_EQ(handle.get(), 2.34f);

  handle = {};
}

TEST(task, multiple_workers_any_execution) {
  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{
          .worker_configs = {{"worker1", async_coro::execution_queues::worker},
                             {"worker2", async_coro::execution_queues::worker},
                             {"worker3", async_coro::execution_queues::worker},
                             {"worker4", async_coro::execution_queues::worker},
                             {"worker5", async_coro::execution_queues::worker}},
          .main_thread_allowed_tasks = async_coro::execution_queues::main})};

  auto routine = []() -> async_coro::task<float> {
    auto handle_void = co_await async_coro::start_task(
        []() -> async_coro::task<void> {
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
          co_return;
        },
        async_coro::execution_queues::worker);

    auto handle_int = co_await async_coro::start_task(
        []() -> async_coro::task<int> {
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
          co_return 3;
        },
        async_coro::execution_queues::worker);

    auto handle_float = co_await async_coro::start_task(
        []() -> async_coro::task<float> {
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
          co_return 2.34f;
        },
        async_coro::execution_queues::worker);

    auto handle_double = co_await async_coro::start_task(
        []() -> async_coro::task<double> {
          std::this_thread::sleep_for(std::chrono::milliseconds{1});
          co_return std::numbers::pi;
        },
        async_coro::execution_queues::worker);

    auto res = co_await (std::move(handle_void) || std::move(handle_int) || std::move(handle_float) || std::move(handle_double));

    float f_res = -1024.f;

    std::visit([&](auto v) {
      if constexpr (!std::is_same_v<decltype(v), std::monostate>) {
        f_res = float(v);
      } else {
        f_res = 1.f;
      }
    },
               res);

    co_return f_res;
  };

  auto handle = scheduler.start_task(routine, async_coro::execution_queues::main);

  std::size_t num_repeats = 0;
  while (!handle.done() && num_repeats++ < 1000000) {
    scheduler.get_execution_system<async_coro::execution_system>().update_from_main();
    std::this_thread::yield();
  }

  ASSERT_TRUE(handle.done());
  EXPECT_GT(handle.get(), 0.f);

  handle = {};
}
