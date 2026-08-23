# Backpressure & Flow Control — Detailed Implementation Plan

## Overview

Add backpressure mechanisms to prevent thread pool exhaustion during market spikes. The system must gracefully reject or delay tasks when queues are saturated, ensuring predictable latency under load.

---

## Architecture Decisions

### 2.1 Core Principles

1. **Predictable latency** — Tasks must complete within bounded time
2. **Graceful degradation** — Reject low-priority tasks when under pressure
3. **No starvation** — High-priority tasks always make progress
4. **Observable** — Metrics expose queue depth, rejection rates, latency percentiles

### 2.2 Integration with Existing System

- Extend `execution_queue_mark` with priority levels
- Extend `execution_system_config` with backpressure parameters
- Reuse existing `atomic_queue` with capacity limits
- Reuse `thread_notifier` for wake-up signaling

---

## 2.3 Core Components

### 2.3.1 Queue Capacity Configuration

Extend `execution_thread_config` with fixed-size, zero-allocation config:

```cpp
// Fixed-size queue configuration (stack-allocated, no heap)
struct execution_queue_config {
  execution_queue_mark mark;
  char name[32];  // Fixed buffer, no std::string
  uint8_t name_len;

  // Maximum number of tasks in the queue before backpressure kicks in
  uint32_t max_capacity = 1024;

  // When queue depth exceeds this ratio (0.0-1.0), start rejecting low-priority tasks
  uint8_t backpressure_threshold_pct = 80;  // 80%

  // Priority levels: 0 = lowest, 9 = highest (default: 5)
  uint8_t default_priority = 5;

  // Maximum number of concurrent tasks allowed on this queue
  uint32_t max_concurrent = 0;  // 0 = unlimited

  // Time-based backpressure: reject tasks older than this
  uint32_t max_wait_time_ms = 5000;  // 5 seconds
};
```

### 2.3.2 Priority-Based Task Scheduling

Extend `execution_queue_mark` with priority:

```cpp
class execution_queue_mark {
 public:
  // Priority levels for task scheduling
  enum class priority : uint8_t {
    background = 0,   // Logging, cleanup, metrics collection
    normal = 5,       // Default task priority
    high = 8,         // Order submission, risk checks
    critical = 9      // Market data processing, circuit breaker trips
  };

  // Task with priority
  struct prioritized_task {
    task_function func;
    priority prio;
    std::chrono::steady_clock::time_point enqueue_time;
  };
};
```

### 2.3.3 Backpressure Manager

```cpp
class backpressure_manager {
 public:
  explicit backpressure_manager(execution_system& system);

  // Check if task can be accepted
  bool try_accept(prioritized_task& task);

  // Reject a task (called when backpressure is active)
  void reject(prioritized_task& task);

  // Get current queue depth for a queue mark
  size_t get_queue_depth(execution_queue_mark mark) const noexcept;

  // Get utilization ratio (0.0 - 1.0)
  double get_utilization(execution_queue_mark mark) const noexcept;

  // Check if backpressure is active
  bool is_backpressure_active(execution_queue_mark mark) const noexcept;

  // Get rejection count
  size_t get_rejection_count(execution_queue_mark mark) const noexcept;

 private:
  void update_metrics();
  void enforce_limits();

  execution_system& _system;
  std::atomic<size_t> _queue_depths[MAX_QUEUES];
  std::atomic<size_t> _rejection_counts[MAX_QUEUES];
  std::atomic<double> _utilization[MAX_QUEUES];
  std::mutex _metrics_mutex;
};
```

### 2.3.4 Execution Queue with Capacity Limits

Modify the existing `task_queue` to support capacity limits:

```cpp
struct task_queue {
  atomic_queue<task_function> queue;
  std::atomic<size_t> current_depth{0};
  std::atomic<size_t> max_depth{0};
  std::atomic<bool> backpressure_active{false};
  std::atomic<size_t> rejection_count{0};

  // Try to push a task; returns false if backpressure is active
  bool try_push(task_function func, execution_queue_mark::priority prio) {
    if (backpressure_active.load(std::memory_order::acquire)) {
      if (prio < execution_queue_mark::priority::high) {
        rejection_count.fetch_add(1, std::memory_order::relaxed);
        return false;
      }
    }

    size_t current = current_depth.load(std::memory_order::relaxed);
    while (current < max_capacity) {
      if (current_depth.compare_exchange_weak(
              current, current + 1,
              std::memory_order::relaxed,
              std::memory_order::relaxed)) {
        queue.push(std::move(func));
        return true;
      }
    }

    // Queue full, reject
    rejection_count.fetch_add(1, std::memory_order::relaxed);
    return false;
  }
};
```

---

## 2.4 Backpressure Policies

### 2.4.1 Threshold-Based Rejection

When queue depth exceeds `backpressure_threshold * max_capacity`:
- Low-priority tasks (`background`, `normal`) are rejected
- High-priority tasks (`high`, `critical`) are still accepted
- Metrics are updated for monitoring

### 2.4.2 Time-Based Rejection

Tasks older than `max_wait_time` are rejected:
- Prevents starvation of newer tasks
- Ensures bounded latency
- Useful for non-critical background tasks

### 2.4.3 Concurrent Task Limits

When `max_concurrent` is set:
- Track running tasks per queue
- Reject new tasks when limit is reached
- Useful for I/O-bound workloads with limited resources

### 2.4.4 Adaptive Backpressure

Dynamic adjustment based on system load:
- Monitor thread CPU utilization
- Adjust `backpressure_threshold` dynamically
- Reduce threshold under high CPU load
- Increase threshold under low CPU load

---

## 2.5 Integration with Scheduler

Modify `scheduler::start_task` to respect backpressure:

```cpp
template <typename R>
task_handle<R> start_task(task_launcher<R> launcher) {
  auto coro = launcher.launch();
  auto handle = coro.release_handle(passkey{this});
  task_handle<R> result{handle, transfer_ownership{}};

  if (!handle.done()) {
    // Check backpressure before scheduling
    auto& bp = _execution_system->get_backpressure_manager();
    if (!bp.try_accept(launcher.get_execution_queue(), launcher.get_priority())) {
      // Backpressure active, reject task
      handle.promise().set_error(io_error::backpressure_rejected);
      return result;
    }

    add_coroutine(handle.promise(), launcher.get_start_function(),
                  launcher.get_execution_queue());
    return result;
  }

  handle.promise().check_exception();
  return result;
}
```

---

## 2.6 Metrics & Observability

### 2.6.1 Backpressure Metrics

```cpp
struct backpressure_metrics {
  size_t queue_depth;
  size_t max_capacity;
  double utilization;
  size_t rejection_count;
  size_t accepted_count;
  std::chrono::milliseconds avg_wait_time;
  std::chrono::milliseconds max_wait_time;
  bool backpressure_active;
};
```

### 2.6.2 Integration with Metrics Collector

```cpp
class metrics_collector {
 public:
  void record_backpressure(execution_queue_mark mark, const backpressure_metrics& m);
  void record_rejection(execution_queue_mark mark, io_error error);
  void record_latency(execution_queue_mark mark, std::chrono::microseconds latency);
};
```

---

## 2.7 File Structure

```
async_coro/
├── include/async_coro/
│   ├── execution_queue_mark.h          ← Extend with priority
│   ├── execution_system.h              ← Add backpressure config
│   ├── backpressure_manager.h
│   ├── backpressure_metrics.h
│   └── io_error.h                      ← Add backpressure_rejected error
├── src/
│   ├── backpressure_manager.cpp
│   └── execution_system.cpp            ← Add backpressure logic
```

---

## 2.8 Testing Plan

| Test | Description |
|------|-------------|
| `test_queue_capacity_limit` | Queue rejects tasks when full |
| `test_backpressure_threshold` | Low-priority tasks rejected above threshold |
| `test_priority_scheduling` | High-priority tasks accepted during backpressure |
| `test_time_based_rejection` | Old tasks rejected after max_wait_time |
| `test_concurrent_task_limit` | Tasks rejected when max_concurrent reached |
| `test_adaptive_backpressure` | Threshold adjusts based on CPU load |
| `test_backpressure_metrics` | Metrics accurately reflect queue state |
| `test_scheduler_backpressure` | Scheduler respects backpressure |
| `test_task_launcher_priority` | Task launcher respects priority |
| `test_multiple_queues` | Backpressure independent per queue |

---

## 2.9 Estimated Effort

| Component | Effort |
|-----------|--------|
| `execution_queue_mark` priority extension | 0.5 day |
| `backpressure_manager` | 1 day |
| `task_queue` capacity limits | 1 day |
| `scheduler` backpressure integration | 0.5 day |
| `metrics_collector` integration | 0.5 day |
| Tests | 1 day |
| **Total** | **~4.5 days** |

---

## 2.10 Dependencies

No external dependencies. Uses existing:
- `atomic_queue`
- `thread_notifier`
- `mutex`
- `metrics_collector` (from Phase 1)
