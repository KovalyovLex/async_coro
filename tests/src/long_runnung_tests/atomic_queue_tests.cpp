#include <async_coro/atomic_queue.h>
#include <async_coro/atomic_stack.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "memory_hooks.h"

class atomic_queue_tests : public ::testing::TestWithParam<std::tuple<uint32_t, uint32_t>> {
};

TEST_P(atomic_queue_tests, int_queue) {
  constexpr uint32_t num_values = 1000000;

  std::atomic_int sum = 0;
  std::atomic_int sum_pushed = 0;

  const auto mem_before = mem_hook::num_allocated.load();
  {
    async_coro::atomic_queue<int> q;

    std::atomic_int num_pops = 0;
    std::atomic_int num_pushes = 0;

    const auto num_cons = std::get<0>(GetParam());
    const auto num_prods = std::get<1>(GetParam());

    ASSERT_GT(num_cons, 0);
    ASSERT_GT(num_prods, 0);

    {
      // jthread is unavailable on some platforms so we use atomic bools here
      std::vector<std::pair<std::thread, std::unique_ptr<std::atomic_bool>>> consumers;
      std::vector<std::thread> prods;

      for (uint32_t i = 0; i < num_cons; i++) {
        auto stop_ptr = std::make_unique<std::atomic_bool>(false);
        auto thread_body = [&q, &sum, &num_pops, stop = stop_ptr.get()]() {
          while (!*stop) {
            int val;
            if (q.try_pop(val)) {
              sum += val;
              num_pops++;
            }
          }
        };

        consumers.emplace_back(thread_body, std::move(stop_ptr));
      }

      auto num_values_by_prod = num_values / num_prods;

      for (uint32_t i = 0; i < num_prods - 1; i++) {
        prods.emplace_back([num_values_by_prod, &q, &num_pushes, &sum_pushed]() {
          for (uint32_t i = 0; i < num_values_by_prod; i++) {
            const int val = i % 4;
            sum_pushed += val;
            q.push(val);
            num_pushes++;
          }
        });
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});

      auto final_portion = num_values_by_prod + (num_values % num_prods);
      for (uint32_t i = 0; i < final_portion; i++) {
        const int val = i % 4;
        sum_pushed += val;
        q.push(val);
        num_pushes++;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});
      for (auto& thread : prods) {
        thread.join();
        thread = {};
      }
      prods.clear();

      int num_fails = 0;
      while (q.has_value() || num_fails++ < 5) {
        std::this_thread::yield();
      }

      for (auto& [thread, stop] : consumers) {
        *stop = true;
        thread.join();
        thread = {};
      }
    }

    EXPECT_EQ(num_pushes.load(), num_values);

    EXPECT_EQ(num_pops.load(), num_pushes.load());
  }

  EXPECT_EQ(mem_before, mem_hook::num_allocated.load());

  EXPECT_EQ(sum.load(), sum_pushed.load());
}

TEST_P(atomic_queue_tests, int_stack) {
  constexpr uint32_t num_values = 1000000;

  std::atomic_int sum = 0;
  std::atomic_int sum_pushed = 0;

  const auto mem_before = mem_hook::num_allocated.load();
  {
    async_coro::atomic_stack<int> q;

    std::atomic_int num_pops = 0;
    std::atomic_int num_pushes = 0;

    const auto num_cons = std::get<0>(GetParam());
    const auto num_prods = std::get<1>(GetParam());

    ASSERT_GT(num_cons, 0);
    ASSERT_GT(num_prods, 0);

    {
      // jthread is unavailable on some platforms so we use atomic bools here
      std::vector<std::pair<std::thread, std::unique_ptr<std::atomic_bool>>> consumers;
      std::vector<std::thread> prods;

      for (uint32_t i = 0; i < num_cons; i++) {
        auto stop_ptr = std::make_unique<std::atomic_bool>(false);
        auto thread_body = [&q, &sum, &num_pops, stop = stop_ptr.get()]() {
          while (!*stop) {
            int val;
            if (q.try_pop(val)) {
              sum += val;
              num_pops++;
            }
          }
        };

        consumers.emplace_back(thread_body, std::move(stop_ptr));
      }

      auto num_values_by_prod = num_values / num_prods;

      for (uint32_t i = 0; i < num_prods - 1; i++) {
        prods.emplace_back([num_values_by_prod, &q, &num_pushes, &sum_pushed]() {
          for (uint32_t i = 0; i < num_values_by_prod; i++) {
            const int val = i % 4;
            sum_pushed += val;
            q.push(val);
            num_pushes++;
          }
        });
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});

      auto final_portion = num_values_by_prod + (num_values % num_prods);
      for (uint32_t i = 0; i < final_portion; i++) {
        const int val = i % 4;
        sum_pushed += val;
        q.push(val);
        num_pushes++;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{20});
      for (auto& thread : prods) {
        thread.join();
        thread = {};
      }
      prods.clear();

      int num_fails = 0;
      while (q.has_value() || num_fails++ < 5) {
        std::this_thread::yield();
      }

      for (auto& [thread, stop] : consumers) {
        *stop = true;
        thread.join();
        thread = {};
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
        std::make_tuple(1u, 1u),
        std::make_tuple(1u, 2u),
        std::make_tuple(1u, 3u),
        std::make_tuple(1u, 4u),
        std::make_tuple(1u, 8u),
        std::make_tuple(1u, 16u),
        std::make_tuple(2u, 1u),
        std::make_tuple(2u, 2u),
        std::make_tuple(2u, 3u),
        std::make_tuple(2u, 4u),
        std::make_tuple(2u, 8u),
        std::make_tuple(3u, 1u),
        std::make_tuple(3u, 2u),
        std::make_tuple(3u, 3u),
        std::make_tuple(3u, 4u),
        std::make_tuple(3u, 8u),
        std::make_tuple(4u, 1u),
        std::make_tuple(4u, 2u),
        std::make_tuple(4u, 3u),
        std::make_tuple(4u, 4u),
        std::make_tuple(4u, 8u),
        std::make_tuple(8u, 1u),
        std::make_tuple(8u, 2u),
        std::make_tuple(8u, 3u),
        std::make_tuple(8u, 4u),
        std::make_tuple(8u, 8u)),
    [](const testing::TestParamInfo<atomic_queue_tests::ParamType>& info) {
      return "consumers_" + std::to_string(std::get<0>(info.param)) + "_producers_" + std::to_string(std::get<1>(info.param));
    });
