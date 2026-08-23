# Error Handling & Propagation — Detailed Implementation Plan

## Overview

Add structured error handling with `std::expected` for error propagation, custom error types, and graceful degradation patterns. The system must provide clear error messages and enable users to handle failures appropriately.

---

## Architecture Decisions

### 4.1 Core Principles

1. **Type-safe errors** — Use `std::expected` for compile-time error handling
2. **Structured error codes** — Enum-based error codes with descriptive messages
3. **Graceful degradation** — `value_or()` for default values on failure
4. **Exception integration** — Optional exception throwing for traditional error handling
5. **Error context** — Include source location and correlation IDs in errors

### 4.2 Integration with Existing System

- Extend `task_handle` with `std::expected` support
- Extend `promise_result` with error storage
- Extend `scheduler` with error handlers
- Reuse existing `base_handle` for error propagation

---

## 4.3 Core Components

### 4.3.1 Error Code Enum

```cpp
namespace async_coro {

enum class error_code {
  // General errors
  success = 0,
  invalid_argument,
  out_of_range,
  internal_error,

  // Task errors
  task_cancelled,
  task_timeout,
  task_not_started,
  task_already_started,

  // Execution errors
  execution_queue_full,
  execution_system_not_ready,
  thread_pool_exhausted,

  // I/O errors (from Phase 1)
  connection_refused,
  connection_timeout,
  host_not_found,
  permission_denied,
  network_unreachable,
  operation_canceled,
  broken_pipe,
  connection_reset,

  // Cancellation errors
  cancellation_requested,
  cancellation_timeout,

  // Backpressure errors (from Phase 1)
  backpressure_rejected,
  queue_capacity_exceeded,

  // Resource errors
  resource_not_found,
  resource_already_exists,
  resource_limit_exceeded
};

// Error category for std::error_code integration
class error_category : public std::error_category {
 public:
  const char* name() const noexcept override;
  std::string message(int ev) const override;
};

// Global error function
const std::error_category& system_category() noexcept;

// Helper to create error_code from int
inline std::error_code make_error_code(error_code e) noexcept {
  return {static_cast<int>(e), system_category()};
}

}  // namespace async_coro
```

### 4.3.2 Expected Type

```cpp
namespace async_coro {

// Specialized expected for task_handle
template <typename R>
class expected<task_handle<R>, error_code> {
 public:
  using value_type = task_handle<R>;
  using error_type = error_code;

  // Construct with success value
  expected(task_handle<R> value) noexcept;

  // Construct with error
  expected(error_code error) noexcept;

  // Check if has value
  [[nodiscard]] bool has_value() const noexcept;

  // Get value or throw
  task_handle<R>& value() &;
  task_handle<R> value() &&;
  const task_handle<R>& value() const&;

  // Get error
  error_code error() const&;

  // Get value or default
  task_handle<R> value_or(task_handle<R> default_value) const&;

  // Swap
  void swap(expected& other) noexcept;

 private:
  std::variant<task_handle<R>, error_code> _storage;
};

// Specialization for void
template <>
class expected<task_handle<void>, error_code> {
 public:
  using value_type = void;
  using error_type = error_code;

  // Construct with success (no value)
  expected() noexcept;

  // Construct with error
  expected(error_code error) noexcept;

  // Check if has value
  [[nodiscard]] bool has_value() const noexcept;

  // Get error
  error_code error() const&;

  // Get default (always returns void)
  void value_or() const&;

 private:
  std::optional<error_code> _error;
};

}  // namespace async_coro
```

### 4.3.3 Task Handle Error Support

Extend `task_handle` with error handling:

```cpp
template <typename R>
class task_handle {
 public:
  // Get result or throw on error
  decltype(auto) get_or_throw() & {
    if (_error) {
      throw std::system_error(_error.value(), async_coro::system_category());
    }
    return _handle.promise().get_result_ref();
  }

  // Get result with default on error
  decltype(auto) get_or(R default_value) & {
    if (_error) {
      return default_value;
    }
    return _handle.promise().get_result_ref();
  }

  // Check if task has error
  [[nodiscard]] bool has_error() const noexcept {
    return _error.has_value();
  }

  // Get error code
  [[nodiscard]] error_code error() const& {
    ASYNC_CORO_ASSERT(_error.has_value());
    return _error.value();
  }

  // Set error
  void set_error(error_code code) noexcept {
    _error = code;
  }

 private:
  std::optional<error_code> _error;
};
```

### 4.3.4 Promise Result Error Support

Extend `promise_result` with error storage:

```cpp
template <typename T>
class promise_result : public internal::promise_result_base<T> {
 public:
  // Set error
  void set_error(error_code code) noexcept {
    this->_error = code;
  }

  // Get error
  [[nodiscard]] std::optional<error_code> get_error() const noexcept {
    return this->_error;
  }

  // Check if has error
  [[nodiscard]] bool has_error() const noexcept {
    return this->_error.has_value();
  }

 private:
  std::optional<error_code> _error;
};
```

### 4.3.5 Scheduler Error Handler

```cpp
class scheduler {
 public:
  // Set unhandled exception handler
  void set_unhandled_exception_handler(
      std::function<void(std::exception_ptr)> handler) noexcept;

  // Set error callback for specific error codes
  void set_error_callback(error_code code, std::function<void(error_code)> callback);

  // Get error handler
  [[nodiscard]] std::function<void(std::exception_ptr)> get_unhandled_exception_handler() const noexcept;

 private:
  std::function<void(std::exception_ptr)> _unhandled_exception_handler;
  std::map<error_code, std::function<void(error_code)>> _error_callbacks;
};
```

---

## 4.4 Error Propagation Patterns

### 4.4.1 Task Error Propagation

```cpp
// Task that can fail
task<std::string> fetch_data() {
  try {
    // Simulate network error
    if (network_error) {
      co_return std::unexpected(async_coro::error_code::connection_refused);
    }
    co_return "data";
  } catch (const std::exception& e) {
    co_return std::unexpected(async_coro::error_code::internal_error);
  }
}

// Usage
auto result = co_await fetch_data();
if (result.has_value()) {
  std::cout << result.value() << std::endl;
} else {
  std::cerr << "Error: " << result.error().message() << std::endl;
}
```

### 4.4.2 Graceful Degradation

```cpp
// Use default value on error
auto data = co_await fetch_data().get_or("default_data");
// Always returns a value, uses default on error

// Conditional execution
auto result = co_await fetch_data();
if (result.has_value()) {
  process(result.value());
} else {
  fallback_process();
}
```

### 4.4.3 Exception Integration

```cpp
// Throw on error
try {
  auto data = co_await fetch_data().get_or_throw();
  process(data);
} catch (const std::system_error& e) {
  log_error(e.code().message());
}
```

---

## 4.5 File Structure

```
async_coro/
├── include/async_coro/
│   ├── error_code.h
│   ├── expected.h
│   ├── task_handle.h          ← Extend with error support
│   ├── promise_result.h       ← Extend with error storage
│   └── scheduler.h            ← Add error handlers
├── src/
│   ├── error_code.cpp
│   ├── expected.cpp
│   └── scheduler.cpp          ← Add error handler logic
```

---

## 4.6 Testing Plan

| Test | Description |
|------|-------------|
| `test_error_code_enum` | Error codes defined correctly |
| `test_error_category` | Error category works with std::error_code |
| `test_expected_success` | Expected with success value |
| `test_expected_error` | Expected with error |
| `test_expected_value_or` | Default value on error |
| `test_task_handle_error` | Task handle error propagation |
| `test_task_handle_get_or_throw` | Throw on error |
| `test_task_handle_get_or` | Default value on error |
| `test_promise_result_error` | Promise result error storage |
| `test_scheduler_error_handler` | Unhandled exception handler |
| `test_scheduler_error_callback` | Specific error callbacks |
| `test_error_propagation` | Error propagation through task chain |

---

## 4.7 Estimated Effort

| Component | Effort |
|-----------|--------|
| `error_code` enum and category | 0.5 day |
| `expected` type | 1 day |
| `task_handle` error support | 0.5 day |
| `promise_result` error storage | 0.5 day |
| `scheduler` error handlers | 0.5 day |
| Tests | 1 day |
| **Total** | **~4 days** |

---

## 4.8 Dependencies

No external dependencies. Uses existing:
- `std::expected` (C++23) or custom implementation for C++20
- `std::variant` for storage
- `std::optional` for error storage
- `std::system_error` for error integration
