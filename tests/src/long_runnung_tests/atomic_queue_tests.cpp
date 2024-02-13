#include <async_coro/atomic_queue.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "memory_hooks.h"

class atomic_queue_tests : public ::testing::TestWithParam<std::tuple<int, int>> {
};

TEST_P(atomic_queue_tests, multi) {
  constexpr int num_values = 100000;

  std::atomic_int sum = 0;
  std::atomic_int sum_pushed = 0;

  const auto mem_before = mem_hook::num_allocated.load();
  {
    async_coro::atomic_queue<int> q;

    std::atomic_int num_pops = 0;
    std::atomic_int num_pushes = 0;

    const int num_cons = std::get<0>(GetParam());
    const int num_prods = std::get<1>(GetParam());

    ASSERT_GT(num_cons, 0);
    ASSERT_GT(num_prods, 0);

    {
      std::vector<std::jthread> consumers;
      std::vector<std::jthread> prods;

      for (int i = 0; i < num_cons; i++) {
        consumers.emplace_back([&q, &sum, &num_pops](std::stop_token stoken) {
          while (!stoken.stop_requested()) {
            int val;
            if (q.try_pop(val)) {
              sum += val;
              num_pops++;
            }
          }
        });
      }

      int num_values_by_prod = num_values / num_prods;

      for (int i = 0; i < num_prods - 1; i++) {
        prods.emplace_back([num_values_by_prod, &q, &num_pushes, &sum_pushed]() {
          for (int i = 0; i < num_values_by_prod; i++) {
            const auto val = i % 4;
            sum_pushed += val;
            q.push(val);
            num_pushes++;
          }
        });
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});

      int final_portion = num_values_by_prod + (num_values % num_prods);
      for (int i = 0; i < final_portion; i++) {
        const auto val = i % 4;
        sum_pushed += val;
        q.push(val);
        num_pushes++;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});

      int num_failes = 0;
      while (q.has_value() || num_failes++ < 5) {
        std::this_thread::yield();
      }
    }

    EXPECT_EQ(num_pushes.load(), num_values);

    EXPECT_EQ(num_pops.load(), num_pushes.load());
  }

  EXPECT_EQ(mem_before, mem_hook::num_allocated.load());

  EXPECT_EQ(sum.load(), sum_pushed.load());
}

INSTANTIATE_TEST_SUITE_P(
    atomic_queue,
    atomic_queue_tests,
    ::testing::Values(
        std::make_tuple(1, 1),
        std::make_tuple(1, 2),
        std::make_tuple(1, 3),
        std::make_tuple(1, 4),
        std::make_tuple(1, 8),
        std::make_tuple(1, 16),
        std::make_tuple(2, 1),
        std::make_tuple(2, 2),
        std::make_tuple(2, 3),
        std::make_tuple(2, 4),
        std::make_tuple(2, 8),
        std::make_tuple(2, 16),
        std::make_tuple(3, 1),
        std::make_tuple(3, 2),
        std::make_tuple(3, 3),
        std::make_tuple(3, 4),
        std::make_tuple(3, 8),
        std::make_tuple(3, 16),
        std::make_tuple(4, 1),
        std::make_tuple(4, 2),
        std::make_tuple(4, 3),
        std::make_tuple(4, 4),
        std::make_tuple(4, 8),
        std::make_tuple(4, 16),
        std::make_tuple(8, 1),
        std::make_tuple(8, 2),
        std::make_tuple(8, 3),
        std::make_tuple(8, 4),
        std::make_tuple(8, 8),
        std::make_tuple(8, 16),
        std::make_tuple(16, 1),
        std::make_tuple(16, 2),
        std::make_tuple(16, 3),
        std::make_tuple(16, 4),
        std::make_tuple(16, 8),
        std::make_tuple(16, 16)),
    [](const testing::TestParamInfo<atomic_queue_tests::ParamType>& info) {
      return "consumers_" + std::to_string(std::get<0>(info.param)) + "_produsers_" + std::to_string(std::get<1>(info.param));
    });
