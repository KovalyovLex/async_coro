#include <async_coro/await_callback.h>
#include <async_coro/scheduler.h>
#include <async_coro/start_task.h>
#include <async_coro/switch_to_thread.h>
#include <async_coro/task.h>
#include <async_coro/when_all.h>
#include <gtest/gtest.h>

#include <semaphore>

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

    co_await switch_to_thread(async_coro::execution_thread::worker_thread);

    EXPECT_NE(current_th, std::this_thread::get_id());

    async_done = true;

    co_await switch_to_thread(async_coro::execution_thread::main_thread);

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

    co_await switch_to_thread(async_coro::execution_thread::worker_thread);

    EXPECT_NE(current_th, std::this_thread::get_id());

    co_await switch_to_thread(async_coro::execution_thread::worker_thread);

    async_done = true;

    co_await switch_to_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_await switch_to_thread(async_coro::execution_thread::main_thread);

    EXPECT_EQ(current_th, std::this_thread::get_id());

    co_return 7.14f;
  };

  auto routine = [](auto start) -> async_coro::task<int> {
    const auto current_th = std::this_thread::get_id();

    co_await switch_to_thread(async_coro::execution_thread::main_thread);

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
    auto results = co_await async_coro::when_all(
        co_await async_coro::start_task(routine1()),
        co_await async_coro::start_task(routine2()),
        co_await async_coro::start_task(routine3(), async_coro::execution_thread::worker_thread));

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler;
  scheduler.get_working_queue().set_num_threads(1);

  auto handle = scheduler.start_task(routine());
  EXPECT_FALSE(handle.done());

  // wait for worker thread finish coro
  sema.acquire();

  std::this_thread::sleep_for(std::chrono::milliseconds{1});

  EXPECT_FALSE(handle.done());
  scheduler.update();

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 6);
}

TEST(task, when_all_no_wait) {
  auto routine1 = []() -> async_coro::task<int> {
    co_return 2;
  };

  auto routine2 = []() -> async_coro::task<float> {
    co_return 3.14f;
  };

  auto routine = [&]() -> async_coro::task<int> {
    auto results = co_await async_coro::when_all(
        co_await async_coro::start_task(routine1()),
        co_await async_coro::start_task(routine2()));

    const auto sum = std::apply(
        [&](auto... num) {
          return (int(num) + ...);
        },
        results);
    co_return sum;
  };

  async_coro::scheduler scheduler;
  scheduler.get_working_queue().set_num_threads(1);

  auto handle = scheduler.start_task(routine());

  ASSERT_TRUE(handle.done());
  EXPECT_EQ(handle.get(), 5);
}

TEST(task, task_handle_outlive) {
  static int num_instances = 0;

  struct destructable {
    destructable() { num_instances++; }
    destructable(const destructable&) { num_instances++; }
    ~destructable() { num_instances--; }
  };

  auto routine1 = []() -> async_coro::task<destructable> {
    co_return destructable{};
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

  struct destructable {
    destructable() { num_instances++; }
    destructable(const destructable&) { num_instances++; }
    ~destructable() { num_instances--; }
  };

  std::atomic_bool ready = false;

  auto routine1 = [&]() -> async_coro::task<destructable> {
    co_await switch_to_thread(async_coro::execution_thread::worker_thread);
    ready = true;
    co_await switch_to_thread(async_coro::execution_thread::main_thread);

    co_return destructable{};
  };

  async_coro::scheduler scheduler;
  scheduler.get_working_queue().set_num_threads(1);

  EXPECT_EQ(num_instances, 0);

  {
    auto handle = scheduler.start_task(routine1());

    std::thread th([handle2 = std::move(handle), &ready]() {
      while (!ready) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });

    ASSERT_TRUE(handle.done());

    while (!ready) {
      std::this_thread::yield();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{1});

    scheduler.update();

    th.join();

    scheduler.update();
  }

  EXPECT_EQ(num_instances, 0);
}

TEST(task, task_ref_result) {
  static int num_instances = 0;

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

  auto routine1 = []() -> async_coro::task<const int*> {
    co_return &num_instances;
  };

  async_coro::scheduler scheduler;

  auto handle = scheduler.start_task(routine1());

  ASSERT_TRUE(handle.done());

  EXPECT_EQ(handle.get(), &num_instances);
}
