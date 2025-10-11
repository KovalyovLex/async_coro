
#include <async_coro/thread_safety/light_mutex.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

class light_mutex_mt : public ::testing::TestWithParam<std::uint32_t> {
};

TEST_P(light_mutex_mt, perf_simple_lock) {
  std::mutex m1;
  async_coro::light_mutex m2;

  std::atomic_bool running{true};
  volatile int a = 0;

  using clock = std::chrono::high_resolution_clock;
  constexpr size_t kTests = 10000;

  const auto num_workers = GetParam();
  std::vector<std::thread> workers;

  workers.reserve(num_workers);
  for (std::uint32_t i = 0; i < num_workers; i++) {
    workers.emplace_back([&]() {
      while (running.load(std::memory_order_relaxed)) {
        m1.lock();
        a += 1;
        m1.unlock();
      }
    });
  }

  const auto start_mutex = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    m1.lock();
    a += 1;
    m1.unlock();
  }

  const auto mutex_t = clock::now() - start_mutex;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  running = true;
  a = 0;

  workers.reserve(num_workers);
  for (std::uint32_t i = 0; i < num_workers; i++) {
    workers.emplace_back([&]() {
      while (running.load(std::memory_order_relaxed)) {
        m2.lock();
        a += 1;
        m2.unlock();
      }
    });
  }

  const auto start_light = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    m2.lock();
    a += 1;
    m2.unlock();
  }

  const auto light_t = clock::now() - start_light;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  std::cout << "mutex_t: " << mutex_t.count() << " light_t: " << light_t.count() << "\n";

#if (defined(_MSC_VER) && !defined(_DEBUG)) || defined(NDEBUG)
  EXPECT_GE(mutex_t.count(), light_t.count());
#endif
}

TEST_P(light_mutex_mt, perf_try_lock) {
  std::mutex m1;
  async_coro::light_mutex m2;

  std::atomic_bool running{true};
  volatile int a = 0;

  using clock = std::chrono::high_resolution_clock;
  constexpr size_t kTests = 10000;

  const auto num_workers = GetParam();
  std::vector<std::thread> workers;

  workers.reserve(num_workers);
  for (std::uint32_t i = 0; i < num_workers; i++) {
    workers.emplace_back([&]() {
      while (running.load(std::memory_order_relaxed)) {
        m1.lock();
        a += 1;
        m1.unlock();
      }
    });
  }

  const auto start_mutex = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    if (m1.try_lock()) {
      a += 1;
      m1.unlock();
    }
  }

  const auto mutex_t = clock::now() - start_mutex;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  running = true;
  a = 0;

  workers.reserve(num_workers);
  for (std::uint32_t i = 0; i < num_workers; i++) {
    workers.emplace_back([&]() {
      while (running.load(std::memory_order_relaxed)) {
        m2.lock();
        a += 1;
        m2.unlock();
      }
    });
  }

  const auto start_light = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    if (m2.try_lock()) {
      a += 1;
      m2.unlock();
    }
  }

  const auto light_t = clock::now() - start_light;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  std::cout << "mutex_t: " << mutex_t.count() << " light_t: " << light_t.count() << "\n";

#if (defined(_MSC_VER) && !defined(_DEBUG)) || defined(NDEBUG)
  EXPECT_GE(mutex_t.count(), light_t.count());
#endif
}

INSTANTIATE_TEST_SUITE_P(
    light_mutex_mt,
    light_mutex_mt,
    ::testing::Values(
        0u,
        1u,
        2u,
        3u,
        4u,
        5u,
        6u,
        7u,
        8u,
        9u,
        10u,
        11u,
        12u,
        13u,
        14u,
        15u),
    [](const testing::TestParamInfo<light_mutex_mt::ParamType>& info) {
      return "num_workers_" + std::to_string(info.param);
    });
