#pragma once

#include <async_coro/thread_safety/analysis.h>
#include <async_coro/utils/hardware_interference_size.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace async_coro {

// High-throughput spin lock mutex optimized for moderate-to-high concurrency scenarios.
// Uses three-phase backoff strategy:
//   Phase 1 (N1): Fast spins with cpu_relax()
//   Phase 2 (N2): Exponential backoff with yield
//   Phase 3 (N3): Short sleep before retrying the backoff cycle (skips level 0)
//
// Backoff progression is defined in kBackoffConstants — increasing spins and yields per level.
//
// Key features:
// - Relaxed load + CAS for lock acquisition (acquire memory order on success)
// - Small size (std::atomic_bool) keeps mutex and guarded data on the same cache line,
//   reducing the amount of shared state that must be invalidated during contention
// - Architecture-specific relax instructions (_mm_pause on x86, yield on ARM)
//
// Best for: hot paths with moderate contention where throughput matters more than latency.
// For low-concurrency scenarios, prefer light_mutex which has lower overhead when uncontended.
class CORO_THREAD_CAPABILITY("mutex") spin_mutex {
 public:
  using super = spin_mutex;

  spin_mutex() noexcept = default;
  spin_mutex(const spin_mutex&) = delete;
  spin_mutex(spin_mutex&&) = delete;

  ~spin_mutex() noexcept = default;

  spin_mutex& operator=(const spin_mutex&) = delete;
  spin_mutex& operator=(spin_mutex&&) = delete;

  void lock() noexcept CORO_THREAD_ACQUIRE() {
    if (try_lock()) {
      return;
    }

    size_t backoff_i = 0;
    while (true) {
      auto constant = kBackoffConstants[backoff_i];  // NOLINT(*-constant-array-index)

      for (uint32_t n_spins = 0; n_spins < constant.n_relaxed; n_spins++) {
        cpu_relax();
        if (try_lock()) {
          return;
        }
      }

      for (uint32_t n_yields = 0; n_yields < constant.n_yields; n_yields++) {
        std::this_thread::yield();
        if (try_lock()) {
          return;
        }
      }

      backoff_i++;
      if (backoff_i == kBackoffConstants.size()) {
        // Phase 3: Short sleep before retrying the backoff cycle
        std::this_thread::sleep_for(kSleepDuration);

        // Skip fast step
        backoff_i = 1;
      }
    }
  }

  bool try_lock() noexcept CORO_THREAD_TRY_ACQUIRE(true) {
    bool expected = false;
    return _lock.load(std::memory_order_relaxed) == expected &&
           _lock.compare_exchange_strong(
               expected, true, std::memory_order_acquire, std::memory_order_relaxed);
  }

  void unlock() noexcept CORO_THREAD_RELEASE() {
    _lock.store(false, std::memory_order_release);
  }

 private:
  // Architecture-specific CPU relax instruction
  static void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#else
    // Fallback for other architectures
    volatile int i = 0;
    i++;
#endif
  }

  struct BackOffConstant {
    uint32_t n_relaxed;
    uint32_t n_yields;
  };

  static constexpr std::array kBackoffConstants = {
      BackOffConstant{.n_relaxed = 2U, .n_yields = 1},
      BackOffConstant{.n_relaxed = 4U, .n_yields = 1},
      BackOffConstant{.n_relaxed = 4U, .n_yields = 1},
      BackOffConstant{.n_relaxed = 8U, .n_yields = 1},
      BackOffConstant{.n_relaxed = 8U, .n_yields = 2},
      BackOffConstant{.n_relaxed = 16U, .n_yields = 1},
      BackOffConstant{.n_relaxed = 16U, .n_yields = 2},
  };

  static constexpr std::chrono::nanoseconds kSleepDuration{50};  // Sleep duration after exhausting exponential backoff

  std::atomic_bool _lock{false};
};

}  // namespace async_coro
