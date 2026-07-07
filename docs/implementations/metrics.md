# Observability & Metrics — Detailed Implementation Plan

## Overview

Add comprehensive metrics collection, structured logging, and tracing capabilities to enable production monitoring and debugging. The system must expose task execution latency, queue depth, thread utilization, and cancellation rates.

---

## Architecture Decisions

### 3.1 Core Principles

1. **Zero-cost by default** — Metrics collection is disabled unless explicitly enabled
2. **Low overhead** — Metrics collection must not impact latency
3. **Flexible exporters** — Support Prometheus, OpenTelemetry, custom sinks
4. **Structured logging** — JSON-formatted logs with correlation IDs
5. **Request tracing** — End-to-end request tracking across coroutines

### 3.2 Integration with Existing System

- Extend `execution_system_config` with metrics options
- Extend `scheduler` with metrics hooks
- Reuse existing `atomic_queue` for metric events
- Reuse `thread_notifier` for metric flush signaling

---

## 3.3 Core Components

### 3.3.1 Metrics Collector Interface

```cpp
namespace async_coro::metrics {

// Metric types
enum class metric_type {
  counter,      // Monotonically increasing
  gauge,        // Current value (can go up/down)
  histogram,    // Distribution of values
  summary       // Percentile-based distribution
};

// Base metric interface
class metric {
 public:
  virtual ~metric() = default;
  virtual metric_type type() const noexcept = 0;
  virtual void record(double value) = 0;
  virtual void increment(double amount = 1.0) = 0;
  virtual void decrement(double amount = 1.0) = 0;
};

// Counter metric
class counter : public metric {
 public:
  explicit counter(std::string name);
  metric_type type() const noexcept override { return metric_type::counter; }
  void record(double value) override;
  void increment(double amount = 1.0) override;
  void decrement(double amount = 1.0) override;
  [[nodiscard]] double get() const noexcept;

 private:
  std::string _name;
  std::atomic<int64_t> _value{0};
};

// Gauge metric
class gauge : public metric {
 public:
  explicit gauge(std::string name);
  metric_type type() const noexcept override { return metric_type::gauge; }
  void record(double value) override;
  void increment(double amount = 1.0) override;
  void decrement(double amount = 1.0) override;
  [[nodiscard]] double get() const noexcept;

 private:
  std::string _name;
  std::atomic<double> _value{0.0};
};

// Histogram metric
class histogram : public metric {
 public:
  explicit histogram(std::string name, std::vector<double> buckets);
  metric_type type() const noexcept override { return metric_type::histogram; }
  void record(double value) override;
  void increment(double amount = 1.0) override;
  void decrement(double amount = 1.0) override;
  [[nodiscard]] std::vector<std::pair<double, uint64_t>> get_buckets() const;

 private:
  std::string _name;
  std::vector<double> _buckets;
  std::vector<std::atomic<uint64_t>> _counts;
};

}  // namespace async_coro::metrics
```

### 3.3.2 Metrics Collector

```cpp
namespace async_coro::metrics {

class metrics_collector {
 public:
  explicit metrics_collector(scheduler& sched);
  ~metrics_collector() noexcept;

  // Register a metric
  void register_metric(std::shared_ptr<metric> m);

  // Get a metric by name
  std::shared_ptr<metric> get_metric(const std::string& name);

  // Export metrics to a sink
  void export_to(std::shared_ptr<metric_exporter> exporter);

  // Flush pending metrics
  void flush();

  // Enable/disable metrics collection
  void set_enabled(bool enabled) noexcept;
  [[nodiscard]] bool is_enabled() const noexcept;

 private:
  void metrics_loop();

  scheduler& _sched;
  std::mutex _metrics_mutex;
  std::map<std::string, std::shared_ptr<metric>> _metrics;
  std::atomic<bool> _enabled{false};
  std::thread _metrics_thread;
  std::atomic<bool> _is_stopping{false};
};

}  // namespace async_coro::metrics
```

### 3.3.3 Metric Exporters

```cpp
namespace async_coro::metrics {

// Base exporter interface
class metric_exporter {
 public:
  virtual ~metric_exporter() = default;
  virtual void export_metrics(const std::map<std::string, std::shared_ptr<metric>>& metrics) = 0;
};

// Prometheus exporter
class prometheus_exporter : public metric_exporter {
 public:
  explicit prometheus_exporter(uint16_t port);
  void export_metrics(const std::map<std::string, std::shared_ptr<metric>>& metrics) override;

 private:
  uint16_t _port;
  std::thread _http_server;
};

// OpenTelemetry exporter
class otel_exporter : public metric_exporter {
 public:
  explicit otel_exporter(const std::string& endpoint);
  void export_metrics(const std::map<std::string, std::shared_ptr<metric>>& metrics) override;

 private:
  std::string _endpoint;
};

// Console exporter (for debugging)
class console_exporter : public metric_exporter {
 public:
  void export_metrics(const std::map<std::string, std::shared_ptr<metric>>& metrics) override;
};

// File exporter (for offline analysis)
class file_exporter : public metric_exporter {
 public:
  explicit file_exporter(const std::string& path);
  void export_metrics(const std::map<std::string, std::shared_ptr<metric>>& metrics) override;

 private:
  std::string _path;
  std::ofstream _file;
};

}  // namespace async_coro::metrics
```

### 3.3.4 Structured Logger

```cpp
namespace async_coro::logging {

enum class log_level {
  trace,
  debug,
  info,
  warning,
  error,
  critical
};

struct log_entry {
  log_level level;
  std::string message;
  std::string source_file;
  uint32_t source_line;
  std::chrono::steady_clock::time_point timestamp;
  std::thread::id thread_id;
  std::string correlation_id;
  std::map<std::string, std::string> attributes;
};

class logger {
 public:
  static logger& instance();

  // Log a message
  void log(log_level level, const std::string& message,
           const std::string& source_file = "", uint32_t source_line = 0);

  // Log with correlation ID
  void log_with_correlation(log_level level, const std::string& message,
                            const std::string& correlation_id,
                            const std::string& source_file = "", uint32_t source_line = 0);

  // Set log level
  void set_level(log_level level) noexcept;
  [[nodiscard]] log_level get_level() const noexcept;

  // Set log output (console, file, etc.)
  void set_output(std::shared_ptr<log_output> output);

 private:
  logger() = default;
  std::mutex _mutex;
  log_level _level{log_level::info};
  std::shared_ptr<log_output> _output;
};

// Log output interfaces
class log_output {
 public:
  virtual ~log_output() = default;
  virtual void write(const log_entry& entry) = 0;
};

// Console output
class console_output : public log_output {
 public:
  void write(const log_entry& entry) override;
};

// File output
class file_output : public log_output {
 public:
  explicit file_output(const std::string& path);
  void write(const log_entry& entry) override;

 private:
  std::string _path;
  std::ofstream _file;
};

// JSON output (for structured logging)
class json_output : public log_output {
 public:
  void write(const log_entry& entry) override;
};

}  // namespace async_coro::logging
```

### 3.3.5 Request Tracer

```cpp
namespace async_coro::tracing {

struct trace_context {
  std::string trace_id;
  std::string span_id;
  std::string parent_span_id;
  bool sampled;
};

class tracer {
 public:
  static tracer& instance();

  // Start a new trace
  trace_context start_trace(const std::string& operation_name);

  // Continue an existing trace
  trace_context continue_trace(const trace_context& parent, const std::string& operation_name);

  // End a trace span
  void end_span(const trace_context& ctx);

  // Get current trace context
  [[nodiscard]] trace_context get_current_context() const;

 private:
  tracer() = default;
  std::mutex _mutex;
  thread_local trace_context _current_context;
};

}  // namespace async_coro::tracing
```

---

## 3.4 Integration with Execution System

### 3.4.1 Execution System Metrics

```cpp
struct execution_system_metrics {
  // Queue metrics
  struct queue_metrics {
    execution_queue_mark mark;
    size_t current_depth;
    size_t max_depth;
    double utilization;
    size_t rejection_count;
    size_t accepted_count;
    std::chrono::milliseconds avg_wait_time;
    std::chrono::milliseconds max_wait_time;
  };

  // Thread metrics
  struct thread_metrics {
    std::thread::id thread_id;
    std::string name;
    double cpu_utilization;
    size_t tasks_processed;
    std::chrono::milliseconds avg_task_duration;
  };

  // Overall system metrics
  struct system_metrics {
    std::chrono::steady_clock::time_point timestamp;
    std::vector<queue_metrics> queues;
    std::vector<thread_metrics> threads;
    size_t total_tasks_processed;
    size_t total_rejections;
    double overall_utilization;
  };
};
```

### 3.4.2 Scheduler Metrics Hooks

```cpp
class scheduler {
 public:
  // Hook for task start
  void on_task_start(base_handle& handle, execution_queue_mark queue);

  // Hook for task completion
  void on_task_complete(base_handle& handle, execution_queue_mark queue,
                        std::chrono::microseconds duration);

  // Hook for task cancellation
  void on_task_cancelled(base_handle& handle, execution_queue_mark queue);

  // Hook for task error
  void on_task_error(base_handle& handle, execution_queue_mark queue,
                     const std::exception_ptr& error);
};
```

---

## 3.5 File Structure

```
async_coro/
├── include/async_coro/
│   ├── metrics/
│   │   ├── metrics_collector.h
│   │   ├── metric.h
│   │   ├── counter.h
│   │   ├── gauge.h
│   │   ├── histogram.h
│   │   ├── metric_exporter.h
│   │   ├── prometheus_exporter.h
│   │   ├── otel_exporter.h
│   │   ├── console_exporter.h
│   │   └── file_exporter.h
│   ├── logging/
│   │   ├── logger.h
│   │   ├── log_entry.h
│   │   ├── log_output.h
│   │   ├── console_output.h
│   │   ├── file_output.h
│   │   └── json_output.h
│   └── tracing/
│       ├── tracer.h
│       └── trace_context.h
├── src/
│   ├── metrics/
│   │   ├── metrics_collector.cpp
│   │   ├── counter.cpp
│   │   ├── gauge.cpp
│   │   ├── histogram.cpp
│   │   ├── prometheus_exporter.cpp
│   │   ├── otel_exporter.cpp
│   │   ├── console_exporter.cpp
│   │   └── file_exporter.cpp
│   ├── logging/
│   │   ├── logger.cpp
│   │   ├── console_output.cpp
│   │   ├── file_output.cpp
│   │   └── json_output.cpp
│   └── tracing/
│       └── tracer.cpp
```

---

## 3.6 Testing Plan

| Test | Description |
|------|-------------|
| `test_counter_metric` | Counter increments/decrements correctly |
| `test_gauge_metric` | Gauge records values correctly |
| `test_histogram_metric` | Histogram buckets populated correctly |
| `test_metrics_collector` | Metrics registered and retrieved |
| `test_prometheus_exporter` | Prometheus format output |
| `test_otel_exporter` | OpenTelemetry format output |
| `test_console_exporter` | Console output format |
| `test_file_exporter` | File output format |
| `test_logger` | Log messages recorded correctly |
| `test_json_output` | JSON format output |
| `test_tracer` | Trace context created and propagated |
| `test_execution_system_metrics` | Queue and thread metrics collected |
| `test_scheduler_hooks` | Task start/complete/cancel hooks work |

---

## 3.7 Estimated Effort

| Component | Effort |
|-----------|--------|
| `metric` base classes (counter, gauge, histogram) | 1 day |
| `metrics_collector` | 1 day |
| `metric_exporter` interfaces (Prometheus, OTel, Console, File) | 1.5 days |
| `logger` and `log_output` interfaces | 1 day |
| `tracer` and `trace_context` | 0.5 day |
| Integration with execution system | 1 day |
| Tests | 1 day |
| **Total** | **~7 days** |

---

## 3.8 Dependencies

- **Prometheus** — Optional, for Prometheus exporter (header-only client library)
- **OpenTelemetry** — Optional, for OTel exporter (C++ SDK)
- **nlohmann/json** — Optional, for JSON logging (header-only)

No external dependencies required for core functionality.
