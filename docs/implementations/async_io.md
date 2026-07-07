# Async I/O — Detailed Implementation Plan (Refined)

> **Status:** Refined — validated against existing codebase.
> **Last updated:** 2026-07-07

## Overview

This plan covers async I/O for the `async_coro` library. The codebase **already has** a complete TCP networking stack in `examples/server/lib/` (`server::socket_layer` and `server::http1`). This plan focuses on:

1. **What already exists** — documented for reference
2. **What's genuinely new** — `server::io::file`, io_uring backend
3. **What to remove** — duplicates from the original plan

---

## What Already Exists (Do NOT Reimplement)

### 2.1 `server::socket_layer` — Complete TCP Stack

| File | Purpose |
|------|---------|
| `examples/server/lib/include/server/socket_layer/reactor.h` | epoll/kqueue event loop (one per reactor thread) |
| `examples/server/lib/include/server/socket_layer/connection.h` | TCP socket wrapper with async `read_buffer`/`write_buffer` via `co_await` |
| `examples/server/lib/include/server/socket_layer/listener.h` | TCP listener for accepting connections |
| `examples/server/lib/include/server/socket_layer/connection_id.h` | Platform-abstracted socket FD wrapper |
| `examples/server/lib/include/server/socket_layer/ssl_connection.h` | OpenSSL TLS wrapper with async handshake |
| `examples/server/lib/include/server/socket_layer/ssl_context.h` | SSL context management |
| `examples/server/lib/include/server/socket_layer/socket_config.h` | Platform detection macros (EPOLL/KQUEUE/WIN_SOCKET) |
| `examples/server/lib/src/socket_layer/reactor.cpp` | epoll_wait/kevent polling loop |
| `examples/server/lib/src/socket_layer/connection.cpp` | Non-blocking read/write with SSL support |
| `examples/server/lib/src/socket_layer/listener.cpp` | Socket creation, bind, listen, accept |
| `examples/server/lib/src/tcp_server.cpp` | Multi-reactor TCP server with signal handling |

**Key design patterns already in use:**
- `connection::read_buffer()` / `connection::write_buffer()` return `async_coro::task<expected<...>>`
- Reactor threads run `process_loop()` with configurable sleep
- `continue_callback_t` pattern for resuming coroutines on I/O completion
- `ssl_connection` wraps OpenSSL with `wants_read`/`wants_write` state machine
- `server::utils::expected` (custom expected type) used for error propagation

### 2.2 `server::http1` — Complete HTTP Stack

| File | Purpose |
|------|---------|
| `examples/server/lib/include/server/http1/http_server.h` | HTTP/1.1 server with routing |
| `examples/server/lib/include/server/http1/http_client.h` | HTTP client for request/response exchange |
| `examples/server/lib/include/server/http1/request.h` / `response.h` | HTTP request/response parsing |
| `examples/server/lib/include/server/http1/router.h` | URL routing |
| `examples/server/lib/include/server/http1/compression_negotiation.h` | gzip/br/zstd negotiation |
| `examples/server/lib/src/http1/` | HTTP parsing and serialization |

### 2.3 `server::core` — Connection Interfaces

| File | Purpose |
|------|---------|
| `examples/server/lib/include/server/core/i_read_connection.h` | Abstract read interface (`read_buffer` → `task<expected<size_t, string>>`) |
| `examples/server/lib/include/server/core/i_write_connection.h` | Abstract write interface (`write_buffer` → `task<expected<void, string>>`) |

### 2.4 `async_coro` Core — Execution Infrastructure

| File | Purpose |
|------|---------|
| `async_coro/include/async_coro/scheduler.h` | Coroutine scheduler with execution system |
| `async_coro/include/async_coro/execution_system.h` | Multi-threaded execution with worker pools |
| `async_coro/include/async_coro/i_execution_system.h` | Abstract execution interface |
| `async_coro/include/async_coro/execution_queue_mark.h` | Queue identification and masking |
| `async_coro/include/async_coro/atomic_queue.h` | Lock-free queue for task distribution |
| `async_coro/include/async_coro/thread_notifier.h` | Efficient thread wake-up |
| `async_coro/include/async_coro/await/` | Existing awaitables: sleep, cancel, switch_to_queue, etc. |

---

## What's Genuinely New (Implement These)

### 3.1 `server::io::file` — Async File I/O (NEW)

No existing file I/O abstraction. This is the primary new component.

```cpp
namespace server::io {

// Async file operations using epoll (Linux) or kqueue (macOS)
// For production low-latency: io_uring AIO (Linux 5.1+)

class file {
 public:
  // Open file asynchronously (non-blocking)
  static auto open(const std::string& path, std::ios::openmode mode) noexcept;

  // Read all data into a buffer
  auto read_all() noexcept;

  // Write data
  auto write(std::span<const uint8_t> data) noexcept;

  // Flush
  auto flush() noexcept;

  // Close
  void close() noexcept;
};

}  // namespace server::io
```

**Use cases:**
- Market data file ingestion (tick data, order books)
- Log writing (structured logs with correlation IDs)
- Configuration file loading

**Implementation approach:**
- Use existing `reactor` pattern from `server::socket_layer` (epoll/kqueue)
- File descriptors are pollable on Linux (epoll supports regular files)
- For io_uring: `IORING_OP_READ` / `IORING_OP_WRITE` with fixed files

### 3.2 io_uring Backend (Linux, Future Enhancement)

The existing `reactor` uses epoll. io_uring provides lower latency (<1μs) for both network and file I/O.

```cpp
namespace server::socket_layer {

// Proposed: io_uring-based reactor to replace epoll reactor
// This is a performance optimization, not a new abstraction

class io_uring_reactor {
 public:
  explicit io_uring_reactor(scheduler& sched);

  // Submit operations to io_uring SQ
  void submit_read(int fd, void* buf, size_t count, uint64_t offset);
  void submit_write(int fd, const void* buf, size_t count, uint64_t offset);
  void submit_connect(int fd, const struct sockaddr* addr, socklen_t len);
  void submit_accept(int listen_fd, struct sockaddr* addr, socklen_t* len);

  // Poll CQ and resume coroutines
  void process_loop(std::chrono::nanoseconds max_wait);

 private:
  struct io_uring_sq_uring* _ring;
  std::vector<io_callback> _pending_ops;
  std::thread _completion_thread;
};

}  // namespace server::socket_layer
```

**Notes:**
- io_uring works with both sockets AND regular files — making `server::io::file` trivial on Linux 5.1+
- epoll fallback remains for non-Linux platforms
- This is an **optimization** over the existing epoll reactor, not a replacement of the abstraction

### 3.3 Keep I/O Code in `server::` Namespace

All I/O code stays in `examples/server/lib/` under the `server::` namespace. No new `async_coro::io` namespace is created. The existing `server::socket_layer` and `server::http1` are the canonical location for all networking and I/O abstractions.

---

## Awaitable Design (Consistent with Existing Patterns)

All I/O operations follow the existing awaitable pattern already used in `server::socket_layer`:

```cpp
// Pattern from existing connection.cpp:
async_coro::task<expected<size_t, std::string>> connection::read_buffer(std::span<std::byte> bytes) {
  // 1. Try non-blocking read()
  // 2. If EAGAIN/EWOULDBLOCK, register with reactor via continue_after_receive_data
  // 3. co_await await_callback_with_result<reactor::connection_state>(...)
  // 4. On completion, retry read()
  // 5. Return expected<size_t, std::string>
}
```

The existing `await_callback_with_result` pattern (from `async_coro::await`) is the model to follow.

### Integration with Existing Awaitables

```cpp
// Can mix I/O with existing awaitables
co_await server::io::file::read_all();
co_await async_coro::sleep(std::chrono::milliseconds{100});
co_await async_coro::switch_to_queue(execution_queues::worker);
```

---

## Error Handling (Consistent with Existing Patterns)

The existing codebase uses `server::utils::expected` (a custom expected type) with `std::string` error messages. The new library-level API should follow the same pattern:

```cpp
// Existing pattern (from connection.h):
async_coro::task<expected<size_t, std::string>> read_buffer(std::span<std::byte> bytes);
async_coro::task<expected<void, std::string>> write_buffer(std::span<const std::byte> bytes);

// New library-level API follows same pattern:
async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);
async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> buffer);
```

**Do NOT introduce a new `io_error` enum** — reuse the existing `std::string` error pattern or extend `server::utils::expected` if a typed error is needed.

---

## File Structure (Refined)

All I/O code stays in `examples/server/lib/`:

```
examples/server/lib/
├── include/server/
│   ├── socket_layer/                   ← Existing: TCP networking (reactor, connection, listener, SSL)
│   ├── http1/                          ← Existing: HTTP/1.1 server and client
│   ├── core/                           ← Existing: i_read_connection, i_write_connection
│   └── utils/                          ← Existing: expected, compression, etc.
├── src/
│   ├── socket_layer/                   ← Existing: epoll/kqueue reactor, connection, listener
│   ├── http1/                          ← Existing: HTTP parsing and serialization
│   └── tcp_server.cpp                  ← Existing: multi-reactor server
```

New components (file I/O) extend the existing `server::` namespace:

```
examples/server/lib/
├── include/server/
│   ├── io/                             ← NEW: file I/O namespace
│   │   └── file.h                      ← NEW: async file operations
│   ├── socket_layer/                   ← Existing: TCP networking
│   ├── http1/                          ← Existing: HTTP/1.1
│   ├── core/                           ← Existing: connection interfaces
│   └── utils/                          ← Existing: expected, compression, etc.
├── src/
│   ├── io/                             ← NEW: file I/O implementation
│   │   └── file.cpp
│   ├── socket_layer/                   ← Existing: epoll/kqueue reactor
│   ├── http1/                          ← Existing: HTTP parsing
│   └── tcp_server.cpp                  ← Existing: multi-reactor server
```

---

## Testing Plan (Refined)

| Test | Description | Already Exists? |
|------|-------------|-----------------|
| `test_async_socket_connect` | Non-blocking TCP connect | ✅ Yes (server tests) |
| `test_async_socket_read_write` | Async read/write cycle | ✅ Yes (server tests) |
| `test_async_tcp_server_accept` | Server accepts connections | ✅ Yes (server tests) |
| `test_async_http_client_get` | HTTP GET request/response | ✅ Yes (server tests) |
| `test_async_http_client_post` | HTTP POST with body | ✅ Yes (server tests) |
| `test_server_io_file_read_write` | Async file I/O | ❌ New |
| `test_io_error_handling` | Error propagation | ❌ New (library-level) |
| `test_io_uring_backend` | io_uring specific tests | ❌ New (future) |
| `test_concurrent_io` | Multiple concurrent I/O | ✅ Partial (server tests) |
| `test_io_with_cancel` | Cancel in-flight I/O | ❌ New |
| `test_io_with_switch_queue` | Switch queues after I/O | ❌ New |

---

## Estimated Effort (Refined)

| Component | Effort | Notes |
|-----------|--------|-------|
| `server::io::file` (epoll) | 1 day | Reuse reactor pattern |
| `async_socket` (library-level) | 1 day | Thin wrapper, reuse reactor |
| `io_uring` backend | 2 days | Future optimization |
| Tests | 1 day | Focus on new components |
| **Total** | **~5 days** | |

---

## Dependencies

- **epoll** — Linux (already used by existing reactor)
- **kqueue** — macOS/BSD (already used by existing reactor)
- **io_uring** — Linux kernel 5.1+ (future, header-only)
- **DNS resolution** — `getaddrinfo` (POSIX, already used)

No external dependencies required.
