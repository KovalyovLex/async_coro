#include <async_coro/working_queue.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "working_queue2.h"
#include "working_queue3.h"

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

TEST(working_queue, parallel_for_atomic) {
  async_coro::working_queue queue;

  queue.set_num_threads(3);

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  std::atomic_int max = 0;

  const auto range = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 2, 3, 4, -1, -12, 3};

  queue.parallel_for(
      [&max](int v) {
        auto cur_max = max.load(std::memory_order::acquire);
        while (v > cur_max) {
          if (max.compare_exchange_strong(cur_max, v,
                                          std::memory_order::release)) {
            break;
          }
        }
      },
      range.begin(), range.end());

  EXPECT_EQ(max, 11);
}

TEST(working_queue, parallel_for_many) {
  async_coro::working_queue queue;

  queue.set_num_threads(32);

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{10});

  std::mutex mutex;
  int max = 0;
  size_t num_executions = 0;

  std::vector<int> range(10257, -12);
  std::set<std::thread::id> executed_at;

  range[64] = 56;
  range[32] = 107;

  std::atomic_bool is_executing = true;
  queue.parallel_for(
      [&](int v) {
        EXPECT_TRUE(is_executing);

        std::unique_lock lock{mutex};

        executed_at.emplace(std::this_thread::get_id());

        if (v > max) {
          max = v;
        }
        num_executions++;
      },
      range.begin(), range.end());
  is_executing = false;

  EXPECT_EQ(max, 107);
  EXPECT_EQ(num_executions, range.size());

  max = 0;
  num_executions = 0;

  std::this_thread::sleep_for(std::chrono::milliseconds{1});

  is_executing = true;
  queue.parallel_for(
      [&](int v) {
        EXPECT_TRUE(is_executing);

        std::unique_lock lock{mutex};

        executed_at.emplace(std::this_thread::get_id());

        if (v > max) {
          max = v;
        }
        num_executions++;
      },
      range.begin(), range.end());
  is_executing = false;

  EXPECT_TRUE(executed_at.size() > 1 || (!executed_at.contains(std::this_thread::get_id())));
}
