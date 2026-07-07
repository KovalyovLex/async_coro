# Task Composition & Patterns — Detailed Implementation Plan

## Overview

Add advanced task composition patterns beyond the existing `&&` (all_awaiter) and `||` (any_awaiter). Enable complex async workflows with `race()`, `retry_with_backoff()`, `circuit_breaker()`, and `select()` patterns.

---

## Architecture Decisions

### 5.1 Core Principles

1. **Composable awaitables** — All patterns return awaitables that work with `co_await`
2. **Zero-copy composition** — Avoid unnecessary allocations when combining tasks
3. **Cancellation propagation** — Cancel child tasks when parent is cancelled
4. **Result extraction** — Extract results from composed tasks cleanly
5. **Error handling** — Handle errors from individual tasks gracefully

### 5.2 Integration with Existing System

- Extend `advanced_awaiter` base class
- Reuse existing `all_awaiter` and `any_awaiter`
- Reuse `task_handle` for result extraction
- Reuse `scheduler` for task scheduling

---

## 5.3 Core Components

### 5.3.1 Race Pattern

```cpp
namespace async_coro {

// Race multiple awaitables, first to complete wins
template <class... TAwaiters>
class race_awaiter : public advanced_awaiter<race_awaiter<TAwaiters...>> {
 public:
  using result_type = decltype(get_variant_for_types(
      replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit race_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept;

  bool adv_await_ready() noexcept;
  void cancel_adv_await();
  void adv_await_suspend(continue_callback_ptr continue_f, base_handle& handle);
  result_type adv_await_resume();

 private:
  void on_completion(std::size_t index);
  void cancel_others(std::size_t winner_index);

  std::tuple<TAwaiters...> _awaiters;
  std::atomic<std::size_t> _completed_index{0};
  std::atomic<bool> _was_cancelled{false};
  continue_callback_ptr _continue_f;
  base_handle* _handler;
};

// Helper function
template <class... TAwaiters>
auto race(TAwaiters&&... awaiters) noexcept {
  return race_awaiter<std::decay_t<TAwaiters>...>{
      std::tuple<std::decay_t<TAwaiters>...>{std::forward<TAwaiters>(awaiters)...}};
}

}  // namespace async_coro
```

**Usage example:**
```cpp
// Race two data sources, use whichever completes first
auto data1 = fetch_from_primary();
auto data2 = fetch_from_backup();

auto result = co_await async_coro::race(std::move(data1), std::move(data2));
// result contains the result from whichever completed first
```

### 5.3.2 Retry with Backoff

```cpp
namespace async_coro {

struct retry_config {
  uint32_t max_attempts = 3;
  std::chrono::milliseconds initial_delay = std::chrono::milliseconds{100};
  double backoff_multiplier = 2.0;
  std::chrono::milliseconds max_delay = std::chrono::seconds{10};
  std::function<bool(error_code)> should_retry;

  // Default: retry on network errors
  static retry_config default_network() {
    retry_config config;
    config.should_retry = [](error_code code) {
      return code == error_code::connection_refused ||
             code == error_code::connection_timeout ||
             code == error_code::network_unreachable;
    };
    return config;
  }
};

// Retry a task with exponential backoff
template <typename R>
class retry_awaiter {
 public:
  retry_awaiter(task_launcher<R> launcher, retry_config config) noexcept;

  bool adv_await_ready() noexcept;
  void cancel_adv_await();
  void adv_await_suspend(continue_callback_ptr continue_f, base_handle& handle);
  R adv_await_resume();

 private:
  void attempt();
  void on_attempt_complete(error_code error);
  std::chrono::milliseconds calculate_delay(uint32_t attempt) const;

  task_launcher<R> _launcher;
  retry_config _config;
  uint32_t _current_attempt{0};
  continue_callback_ptr _continue_f;
  base_handle* _handler;
  R _result;
};

// Helper function
template <typename R>
auto retry(task_launcher<R> launcher, retry_config config = retry_config{}) noexcept {
  return retry_awaiter<R>{std::move(launcher), std::move(config)};
}

}  // namespace async_coro
```

**Usage example:**
```cpp
// Retry order submission with backoff
auto order = create_order(...);
auto result = co_await async_coro::retry(
    task_launcher{[order]() { return submit_order(order); }},
    retry_config::default_network());

if (result.has_value()) {
  std::cout << "Order submitted: " << result.value() << std::endl;
}
```

### 5.3.3 Circuit Breaker

```cpp
namespace async_coro {

struct circuit_breaker_config {
  uint32_t failure_threshold = 5;
  std::chrono::milliseconds reset_timeout = std::chrono::seconds{30};
  uint32_t half_open_max_calls = 1;
};

enum class circuit_state {
  closed,      // Normal operation
  open,        // Failing, reject requests
  half_open    // Testing if service recovered
};

class circuit_breaker {
 public:
  explicit circuit_breaker(circuit_breaker_config config = circuit_breaker_config{});

  // Check if request can proceed
  bool try_acquire();

  // Record success
  void record_success();

  // Record failure
  void record_failure();

  // Get current state
  [[nodiscard]] circuit_state state() const noexcept;

  // Reset circuit breaker
  void reset() noexcept;

 private:
  circuit_breaker_config _config;
  std::atomic<uint32_t> _failure_count{0};
  std::atomic<circuit_state> _state{circuit_state::closed};
  std::chrono::steady_clock::time_point _last_failure_time;
  std::atomic<uint32_t> _half_open_calls{0};
};

// Awaitable that wraps a task with circuit breaker
template <typename R>
class circuit_breaker_awaiter {
 public:
  circuit_breaker_awaiter(task_launcher<R> launcher,
                          std::shared_ptr<circuit_breaker> breaker) noexcept;

  bool adv_await_ready() noexcept;
  void cancel_adv_await();
  void adv_await_suspend(continue_callback_ptr continue_f, base_handle& handle);
  R adv_await_resume();

 private:
  task_launcher<R> _launcher;
  std::shared_ptr<circuit_breaker> _breaker;
  continue_callback_ptr _continue_f;
  base_handle* _handler;
  R _result;
};

// Helper function
template <typename R>
auto with_circuit_breaker(task_launcher<R> launcher,
                          std::shared_ptr<circuit_breaker> breaker) noexcept {
  return circuit_breaker_awaiter<R>{std::move(launcher), std::move(breaker)};
}

}  // namespace async_coro
```

**Usage example:**
```cpp
// Protect against cascading failures
auto breaker = std::make_shared<circuit_breaker>();

auto result = co_await async_coro::with_circuit_breaker(
    task_launcher{[&]() { return call_external_service(); }},
    breaker);

if (result.has_value()) {
  process(result.value());
}
```

### 5.3.4 Select Pattern

```cpp
namespace async_coro {

// Select from multiple awaitables with priority
template <class... TAwaiters>
class select_awaiter : public advanced_awaiter<select_awaiter<TAwaiters...>> {
 public:
  using result_type = decltype(get_variant_for_types(
      replace_void_type_in_holder(types_holder<typename TAwaiters::result_type...>{})));

  explicit select_awaiter(std::tuple<TAwaiters...>&& awaiters) noexcept;

  bool adv_await_ready() noexcept;
  void cancel_adv_await();
  void adv_await_suspend(continue_callback_ptr continue_f, base_handle& handle);
  result_type adv_await_resume();

 private:
  void on_completion(std::size_t index);
  void cancel_others(std::size_t winner_index);

  std::tuple<TAwaiters...> _awaiters;
  std::atomic<std::size_t> _completed_index{0};
  std::atomic<bool> _was_cancelled{false};
  continue_callback_ptr _continue_f;
  base_handle* _handler;
};

// Helper function (same as race, but with explicit priority)
template <class... TAwaiters>
auto select(TAwaiters&&... awaiters) noexcept {
  return select_awaiter<std::decay_t<TAwaiters>...>{
      std::tuple<std::decay_t<TAwaiters>...>{std::forward<TAwaiters>(awaiters)...}};
}

}  // namespace async_coro
```

**Usage example:**
```cpp
// Select from multiple data sources with priority
auto primary = fetch_from_primary();
auto backup = fetch_from_backup();
auto cache = fetch_from_cache();

auto result = co_await async_coro::select(std::move(primary),
                                           std::move(backup),
                                           std::move(cache));
// Returns result from highest priority source that completed
```

### 5.3.5 Timeout Pattern

```cpp
namespace async_coro {

// Timeout a task after a duration
template <typename R>
class timeout_awaiter {
 public:
  timeout_awaiter(task_launcher<R> launcher, std::chrono::steady_clock::duration timeout) noexcept;

  bool adv_await_ready() noexcept;
  void cancel_adv_await();
  void adv_await_suspend(continue_callback_ptr continue_f, base_handle& handle);
  R adv_await_resume();

 private:
  void on_timeout();
  void on_completion();

  task_launcher<R> _launcher;
  std::chrono::steady_clock::time_point _deadline;
  std::atomic<bool> _timed_out{false};
  continue_callback_ptr _continue_f;
  base_handle* _handler;
  R _result;
};

// Helper function
template <typename R>
auto with_timeout(task_launcher<R> launcher, std::chrono::steady_clock::duration timeout) noexcept {
  return timeout_awaiter<R>{std::move(launcher), timeout};
}

}  // namespace async_coro
```

**Usage example:**
```cpp
// Timeout long-running task
auto result = co_await async_coro::with_timeout(
    task_launcher{[&]() { return long_running_operation(); }},
    std::chrono::seconds{5});

if (result.has_value()) {
  process(result.value());
} else {
  std::cerr << "Operation timed out" << std::endl;
}
```

---

## 5.4 File Structure

```
async_coro/
├── include/async_coro/
│   ├── await/
│   │   ├── race.h
│   │   ├── retry.h
│   │   ├── circuit_breaker.h
│   │   ├── select.h
│   │   └── timeout.h
│   └── internal/
│       ├── race_awaiter.h
│       ├── retry_awaiter.h
│       ├── circuit_breaker_awaiter.h
│       ├── select_awaiter.h
│       └── timeout_awaiter.h
├── src/
│   └── await/
│       ├── race.cpp
│       ├── retry.cpp
│       ├── circuit_breaker.cpp
│       ├── select.cpp
│       └── timeout.cpp
```

---

## 5.5 Testing Plan

| Test | Description |
|------|-------------|
| `test_race_first_completion` | First task to complete wins |
| `test_race_all_cancel` | All tasks cancelled when one completes |
| `test_retry_success` | Task succeeds on first attempt |
| `test_retry_backoff` | Exponential backoff between attempts |
| `test_retry_max_attempts` | Stop after max attempts |
| `test_retry_should_retry` | Only retry on specific errors |
| `test_circuit_breaker_closed` | Normal operation in closed state |
| `test_circuit_breaker_open` | Reject requests in open state |
| `test_circuit_breaker_half_open` | Test recovery in half-open state |
| `test_select_priority` | Highest priority source wins |
| `test_timeout_success` | Task completes before timeout |
| `test_timeout_failure` | Task cancelled on timeout |

---

## 5.6 Estimated Effort

| Component | Effort |
|-----------|--------|
| `race_awaiter` | 1 day |
| `retry_awaiter` | 1.5 days |
| `circuit_breaker` | 1 day |
| `select_awaiter` | 0.5 day |
| `timeout_awaiter` | 0.5 day |
| Tests | 1.5 days |
| **Total** | **~6 days** |

---

## 5.7 Dependencies

No external dependencies. Uses existing:
- `advanced_awaiter` base class
- `task_handle` for result extraction
- `scheduler` for task scheduling
- `error_code` from Phase 2
