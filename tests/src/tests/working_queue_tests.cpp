#include <async_coro/working_queue.h>
#include <gtest/gtest.h>

#include <initializer_list>

TEST(working_queue, create_many_threads_one_by_one) {
  async_coro::working_queue queue;

  for (uint32_t n = 0; n < 32; n += 1) {
    queue.set_num_threads(n);
  }

  queue.set_num_threads(1);

  std::atomic_bool executed = false;

  queue.execute([&executed]() { executed = true; });

  std::this_thread::sleep_for(std::chrono::milliseconds{20});

  if (!executed) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  EXPECT_TRUE(executed);
}

TEST(working_queue, create_many_threads) {
  async_coro::working_queue queue;

  for (uint32_t n = 0; n < 32; n += 2) {
    queue.set_num_threads(n);
  }

  queue.set_num_threads(2);

  std::atomic_bool executed = false;

  queue.execute([&executed]() { executed = true; });

  std::this_thread::sleep_for(std::chrono::milliseconds{20});

  if (!executed) {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  EXPECT_TRUE(executed);

  queue.set_num_threads(0);
}

TEST(working_queue, parallel_for) {
  async_coro::working_queue queue;

  queue.set_num_threads(2);

  std::atomic_int max = 0;

  const auto range = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  queue.parallel_for(
      [&max](int v) {
        auto cur_max = max.load(std::memory_order_acquire);
        while (v > cur_max) {
          if (max.compare_exchange_weak(cur_max, v,
                                        std::memory_order_release)) {
            break;
          }
          cur_max = max.load(std::memory_order_acquire);
        }
      },
      range.begin(), range.end());

  EXPECT_EQ(max, 10);
}
