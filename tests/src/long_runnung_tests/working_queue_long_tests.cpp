#include <async_coro/working_queue.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include "working_queue2.h"
#include "working_queue3.h"

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

  const async_coro::unique_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time.count() << ", regular time: " << seq_time.count() << ", ratio: " << ((float)parallel_time.count() / seq_time.count()) << std::endl;
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

  const async_coro::unique_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time.count() << ", regular time: " << seq_time.count() << ", ratio: " << ((float)parallel_time.count() / seq_time.count()) << std::endl;
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

  const async_coro::unique_function<void(int)> f = [&](int) {
    EXPECT_TRUE(is_executing);
  };

  is_executing = true;
  const auto t2 = std::chrono::steady_clock::now();
  for (int v : range) {
    f(v);
  }

  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);
  is_executing = false;

  std::cout << "Parallel time: " << parallel_time.count() << ", regular time: " << seq_time.count() << ", ratio: " << ((float)parallel_time.count() / seq_time.count()) << std::endl;
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

  const async_coro::unique_function f = [&](const int& v) {
    const auto idx = &v - range.data();
    range_copy[idx] = v;
  };

  const auto t2 = std::chrono::steady_clock::now();
  for (auto& v : range) {
    f(v);
  }
  const auto seq_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2);

  std::cout << "Parallel time: " << parallel_time.count() << ", regular time: " << seq_time.count() << ", ratio: " << ((float)parallel_time.count() / seq_time.count()) << std::endl;
}

INSTANTIATE_TEST_SUITE_P(
    mt,
    working_queue_speed_tests,
    ::testing::Values(
        std::make_tuple(0),
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
      return "num_workers_" + std::to_string(std::get<0>(info.param));
    });

//

INSTANTIATE_TEST_SUITE_P(
    mt,
    working_queue_tests,
    ::testing::Values(
        std::make_tuple(0, (uint32_t)-1),
        std::make_tuple(0, (uint32_t)10),
        std::make_tuple(0, (uint32_t)128),
        std::make_tuple(0, (uint32_t)512),
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
      return "num_workers_" + std::to_string(std::get<0>(info.param)) + "_n_bucket_" + std::to_string(std::get<1>(info.param));
    });
