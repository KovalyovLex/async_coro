#include <async_coro/thread_safety/spin_mutex.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

class spin_mutex_mt : public ::testing::TestWithParam<std::uint32_t> {
};

TEST_P(spin_mutex_mt, perf_simple_lock) {
  std::mutex m1;
  async_coro::spin_mutex m2;

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
        a = a + 1;
        m1.unlock();
      }
    });
  }

  const auto start_mutex = clock::now();

  m1.lock();
  a = 0;
  m1.unlock();

  for (size_t i = 0; i < kTests; i++) {
    m1.lock();
    a = a + 1;
    m1.unlock();
  }

  m1.lock();
  const auto total_mutex_locks = a;
  m1.unlock();

  const auto mutex_t = clock::now() - start_mutex;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  running = true;

  a = 0;

  for (std::uint32_t i = 0; i < num_workers; i++) {
    workers.emplace_back([&]() {
      while (running.load(std::memory_order_relaxed)) {
        m2.lock();
        a = a + 1;
        m2.unlock();
      }
    });
  }

  const auto start_spin = clock::now();

  m2.lock();
  a = 0;
  m2.unlock();

  for (size_t i = 0; i < kTests; i++) {
    m2.lock();
    a = a + 1;
    m2.unlock();
  }

  m2.lock();
  const auto total_spin_locks = a;
  m2.unlock();

  const auto spin_t = clock::now() - start_spin;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  double mutex_sec = std::chrono::duration<double>(mutex_t).count();
  double spin_sec = std::chrono::duration<double>(spin_t).count();

  std::cout << "mutex_t: " << mutex_t.count() << " spin_t: " << spin_t.count() << "\n";
  std::cout << "total_mutex_locks: " << total_mutex_locks << " total_spin_locks: " << total_spin_locks << "\n";
  std::cout << "throughput_mutex: " << (total_mutex_locks / mutex_sec) << " throughput_spin: " << (total_spin_locks / spin_sec) << "\n";
}

TEST_P(spin_mutex_mt, perf_try_lock) {
  std::mutex m1;
  async_coro::spin_mutex m2;

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
        a = a + 1;
        m1.unlock();
      }
    });
  }

  m1.lock();
  a = 0;
  m1.unlock();

  const auto start_mutex = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    if (m1.try_lock()) {
      a = a + 1;
      m1.unlock();
    }
  }

  m1.lock();
  const auto total_mutex_locks = a;
  m1.unlock();

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
        a = a + 1;
        m2.unlock();
      }
    });
  }

  m2.lock();
  a = 0;
  m2.unlock();

  const auto start_spin = clock::now();

  for (size_t i = 0; i < kTests; i++) {
    if (m2.try_lock()) {
      a = a + 1;
      m2.unlock();
    }
  }

  m2.lock();
  const auto total_spin_locks = a;
  m2.unlock();

  const auto spin_t = clock::now() - start_spin;

  running = false;
  for (auto& worker : workers) {
    worker.join();
  }
  workers.clear();

  double mutex_sec = std::chrono::duration<double>(mutex_t).count();
  double spin_sec = std::chrono::duration<double>(spin_t).count();

  std::cout << "mutex_t: " << mutex_t.count() << " spin_t: " << spin_t.count() << "\n";
  std::cout << "total_mutex_locks: " << total_mutex_locks << " total_spin_locks: " << total_spin_locks << "\n";
  std::cout << "throughput_mutex: " << (total_mutex_locks / mutex_sec) << " throughput_spin: " << (total_spin_locks / spin_sec) << "\n";
}

TEST_P(spin_mutex_mt, correctness_lock_unlock) {
  if (GetParam() == 0) {
    return;
  }

  async_coro::spin_mutex mtx;
  std::atomic<int> counter{0};

  constexpr int kIterations = 1000;
  std::vector<std::thread> workers;

  workers.reserve(GetParam());
  for (std::uint32_t i = 0; i < GetParam(); ++i) {
    workers.emplace_back([&]() {
      for (int j = 0; j < kIterations; ++j) {
        mtx.lock();
        counter++;
        mtx.unlock();
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(counter.load(), GetParam() * kIterations);
}

TEST_P(spin_mutex_mt, correctness_try_lock) {
  if (GetParam() == 0) {
    return;
  }

  async_coro::spin_mutex mtx;
  std::atomic<int> locked_count{0};

  constexpr int kIterations = 1000;
  std::vector<std::thread> workers;

  workers.reserve(GetParam());
  for (std::uint32_t i = 0; i < GetParam(); ++i) {
    workers.emplace_back([&]() {
      for (int j = 0; j < kIterations; ++j) {
        if (mtx.try_lock()) {
          locked_count++;
          mtx.unlock();
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  // At least some locks should have succeeded
  EXPECT_GT(locked_count.load(), 0);
}

INSTANTIATE_TEST_SUITE_P(
    spin_mutex_mt,
    spin_mutex_mt,
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
    [](const testing::TestParamInfo<spin_mutex_mt::ParamType>& info) {
      return "num_workers_" + std::to_string(info.param);
    });
