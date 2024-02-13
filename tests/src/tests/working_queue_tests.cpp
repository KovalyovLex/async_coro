#include <async_coro/working_queue.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <iostream>

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
  EXPECT_GT(executed_at.size(), 1);
}

class working_queue_speed_tests : public ::testing::TestWithParam<std::tuple<int>> {
};

TEST_P(working_queue_speed_tests, atomic) {
  async_coro::working_queue queue;

  queue.set_num_threads(std::get<0>(GetParam()));

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  std::vector<int> range(25478212, 1);

  range[64] = 53;
  range[32] = 107;

  std::atomic_bool is_executing = true;
  const auto t1 = std::chrono::steady_clock::now();
  queue.parallel_for(
      [&](int) {
        EXPECT_TRUE(is_executing);
      },
      range.begin(), range.end());
  const auto parallel_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1);
  is_executing = false;

  const async_coro::move_only_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time << ", regular time: " << seq_time << ", ratio: " << (float)parallel_time.count() / seq_time.count() << std::endl;
}

TEST_P(working_queue_speed_tests, dummy) {
  async_coro::working_queue2 queue;

  queue.set_num_threads(std::get<0>(GetParam()));

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  std::vector<int> range(25478212, 1);

  range[64] = 53;
  range[32] = 107;

  std::atomic_bool is_executing = true;
  const auto t1 = std::chrono::steady_clock::now();
  queue.parallel_for(
      [&](int) {
        EXPECT_TRUE(is_executing);
      },
      range.begin(), range.end());
  const auto parallel_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1);
  is_executing = false;

  const async_coro::move_only_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time << ", regular time: " << seq_time << ", ratio: " << (float)parallel_time.count() / seq_time.count() << std::endl;
}

TEST_P(working_queue_speed_tests, moodycamel) {
  async_coro::working_queue3 queue;

  queue.set_num_threads(std::get<0>(GetParam()));

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  std::vector<int> range(25478212, 1);

  range[64] = 53;
  range[32] = 107;

  std::atomic_bool is_executing = true;
  const auto t1 = std::chrono::steady_clock::now();
  queue.parallel_for(
      [&](int) {
        EXPECT_TRUE(is_executing);
      },
      range.begin(), range.end());
  const auto parallel_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1);
  is_executing = false;

  const async_coro::move_only_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time << ", regular time: " << seq_time << ", ratio: " << (float)parallel_time.count() / seq_time.count() << std::endl;
}

class working_queue_tests : public ::testing::TestWithParam<std::tuple<int, uint32_t>> {
};

TEST_P(working_queue_tests, sync_results) {
  async_coro::working_queue queue;

  queue.set_num_threads(std::get<0>(GetParam()));

  // wait for processes init
  std::this_thread::sleep_for(std::chrono::milliseconds{30});

  std::vector<int> range(2039465, 0);

  std::srand(643);
  for (size_t i = 0; i < range.size(); i++) {
    range[i] = std::rand();
  }

  std::vector<int> range_copy;
  range_copy.resize(range.size());

  EXPECT_NE(range_copy, range);

  const auto t1 = std::chrono::steady_clock::now();
  queue.parallel_for(
      [&](const int& v) {
        const auto idx = &v - range.data();
        range_copy[idx] = v;
      },
      range.begin(), range.end(), std::get<1>(GetParam()));
  const auto parallel_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1);

  EXPECT_EQ(range_copy, range);

  std::fill(range_copy.begin(), range_copy.end(), 0);

  EXPECT_NE(range_copy, range);

  const async_coro::move_only_function f = [&](const int& v) {
    const auto idx = &v - range.data();
    range_copy[idx] = v;
  };

  const auto t2 = std::chrono::steady_clock::now();
  for (auto& v : range) {
    f(v);
  }
  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);

  std::cout << "Parallel time: " << parallel_time << ", regular time: " << seq_time << ", ratio: " << (float)parallel_time.count() / seq_time.count() << std::endl;
}

INSTANTIATE_TEST_SUITE_P(
    mt,
    working_queue_speed_tests,
    ::testing::Values(
        std::make_tuple(1),
        std::make_tuple(2),
        std::make_tuple(3),
        std::make_tuple(4),
        std::make_tuple(5),
        std::make_tuple(6),
        std::make_tuple(7),
        std::make_tuple(8),
        std::make_tuple(9),
        std::make_tuple(10),
        std::make_tuple(11),
        std::make_tuple(12),
        std::make_tuple(13),
        std::make_tuple(14),
        std::make_tuple(15),
        std::make_tuple(16)),
    [](const testing::TestParamInfo<working_queue_speed_tests::ParamType>& info) {
      return std::format("num_workers_{}", std::get<0>(info.param));
    });

//

INSTANTIATE_TEST_SUITE_P(
    mt,
    working_queue_tests,
    ::testing::Values(
        std::make_tuple(1, (uint32_t)-1),
        std::make_tuple(1, (uint32_t)10),
        std::make_tuple(1, (uint32_t)128),
        std::make_tuple(1, (uint32_t)512),
        std::make_tuple(2, (uint32_t)-1),
        std::make_tuple(2, (uint32_t)10),
        std::make_tuple(2, (uint32_t)128),
        std::make_tuple(2, (uint32_t)515),
        std::make_tuple(3, (uint32_t)-1),
        std::make_tuple(3, (uint32_t)10),
        std::make_tuple(3, (uint32_t)128),
        std::make_tuple(3, (uint32_t)515),
        std::make_tuple(4, (uint32_t)-1),
        std::make_tuple(4, (uint32_t)10),
        std::make_tuple(4, (uint32_t)128),
        std::make_tuple(4, (uint32_t)515),
        std::make_tuple(5, (uint32_t)-1),
        std::make_tuple(5, (uint32_t)10),
        std::make_tuple(5, (uint32_t)128),
        std::make_tuple(5, (uint32_t)515),
        std::make_tuple(6, (uint32_t)-1),
        std::make_tuple(6, (uint32_t)10),
        std::make_tuple(6, (uint32_t)128),
        std::make_tuple(6, (uint32_t)515),
        std::make_tuple(7, (uint32_t)-1),
        std::make_tuple(7, (uint32_t)10),
        std::make_tuple(7, (uint32_t)128),
        std::make_tuple(7, (uint32_t)515),
        std::make_tuple(8, (uint32_t)-1),
        std::make_tuple(8, (uint32_t)10),
        std::make_tuple(8, (uint32_t)128),
        std::make_tuple(8, (uint32_t)515)),
    [](const testing::TestParamInfo<working_queue_tests::ParamType>& info) {
      return std::format("num_workers_{}_n_bucket_{}", std::get<0>(info.param), std::get<1>(info.param));
    });
