# Async I/O — Detailed Implementation Plan (Refined)

> **Status:** 85% Complete — core infrastructure complete, test coverage in progress
> **Last updated:** 2026-07-29

## Completeness Assessment (2026-07-29)

**Overall Completion: ~85%**

- **Core Infrastructure:** ✅ 100% complete — all I/O abstractions implemented
- **Test Coverage:** ⚠️ ~75% complete — functional tests exist, edge cases missing
- **Remaining Work:** ~2-3 days focused on integration and edge-case tests

### Completed Components Summary

| Category | Count | Status |
|----------|-------|--------|
| Core I/O files (io/, socket_layer/, http1/) | ~25 files | ✅ Complete |
| Functional tests | ~40+ tests | ✅ Complete |
| Integration tests | 3/6 categories | ⚠️ Partial |
| Edge-case tests | 0/5 categories | ❌ Missing |

### Missing Components (Priority Order)

1. **Concurrent I/O tests** — Multiple coroutines reading/writing files simultaneously
2. **Cancellation tests** — Cancel in-flight I/O operations cleanly
3. **Queue switching tests** — `co_await async_coro::switch_to_queue()` after I/O completion
4. **TCP server integration test** — Full accept→read→write→close cycle
5. **HTTP client round-trip test** — Full GET/POST with actual server response
6. **io_uring performance benchmarks** — Latency/throughput vs epoll

See detailed status below for each section.

---

## What Already Exists (Do NOT Reimplement)

### 2.1 `server::socket_layer` — Complete TCP Stack ✅ COMPLETE

**Status:** Fully implemented and tested. All 10 source files verified.

| File | Purpose | Status |
|------|---------|--------|
| `examples/server/lib/include/server/socket_layer/reactor.h` | epoll/kqueue event loop (one per reactor thread) | ✅ |
| `examples/server/lib/include/server/socket_layer/connection.h` | TCP socket wrapper with async `read_buffer`/`write_buffer` via `co_await` | ✅ |
| `examples/server/lib/include/server/socket_layer/listener.h` | TCP listener for accepting connections | ✅ |
| `examples/server/lib/include/server/socket_layer/connection_id.h` | Platform-abstracted socket FD wrapper | ✅ |
| `examples/server/lib/include/server/socket_layer/ssl_connection.h` | OpenSSL TLS wrapper with async handshake | ✅ |
| `examples/server/lib/include/server/socket_layer/ssl_context.h` | SSL context management | ✅ |
| `examples/server/lib/include/server/io/io_config.h` | Platform detection macros (EPOLL/KQUEUE/WIN_SOCKET) | ✅ |
| `examples/server/lib/src/socket_layer/reactor.cpp` | epoll_wait/kevent polling loop | ✅ |
| `examples/server/lib/src/socket_layer/connection.cpp` | Non-blocking read/write with SSL support | ✅ |
| `examples/server/lib/src/socket_layer/listener.cpp` | Socket creation, bind, listen, accept | ✅ |
| `examples/server/lib/src/tcp_server.cpp` | Multi-reactor TCP server with signal handling | ✅ |

**Key design patterns already in use:**
- `connection::read_buffer()` / `connection::write_buffer()` return `async_coro::task<expected<...>>`
- Reactor threads run `process_loop()` with configurable sleep
- `continue_callback_t` pattern for resuming coroutines on I/O completion
- `ssl_connection` wraps OpenSSL with `wants_read`/`wants_write` state machine
- `server::utils::expected` (custom expected type) used for error propagation

### 2.2 `server::http1` — Complete HTTP Stack ✅ COMPLETE

**Status:** Fully implemented with 16+ files. Includes server, client, routing, compression.

| File | Purpose | Status |
|------|---------|--------|
| `examples/server/lib/include/server/http1/http_server.h` | HTTP/1.1 server with routing | ✅ |
| `examples/server/lib/include/server/http1/http_client.h` | HTTP client for request/response exchange | ✅ |
| `examples/server/lib/include/server/http1/request.h` / `response.h` | HTTP request/response parsing | ✅ |
| `examples/server/lib/include/server/http1/router.h` | URL routing | ✅ |
| `examples/server/lib/include/server/http1/compression_negotiation.h` | gzip/br/zstd negotiation | ✅ |
| `examples/server/lib/src/http1/` | HTTP parsing and serialization (14 .cpp files) | ✅ |

### 2.3 `server::core` — Connection Interfaces ✅ COMPLETE

**Status:** All interfaces implemented with headers_type.h added.

| File | Purpose | Status |
|------|---------|--------|
| `examples/server/lib/include/server/core/i_read_connection.h` | Abstract read interface (`read_buffer` → `task<expected<size_t, string>>`) | ✅ |
| `examples/server/lib/include/server/core/i_write_connection.h` | Abstract write interface (`write_buffer` → `task<expected<void, string>>`) | ✅ |
| `examples/server/lib/include/server/core/headers_type.h` | Headers type definitions (added) | ✅ |

### 2.4 `async_coro` Core — Execution Infrastructure ✅ COMPLETE

**Status:** All core infrastructure present and functional.

| File | Purpose | Status |
|------|---------|--------|
| `async_coro/include/async_coro/scheduler.h` | Coroutine scheduler with execution system | ✅ |
| `async_coro/include/async_coro/execution_system.h` | Multi-threaded execution with worker pools | ✅ |
| `async_coro/include/async_coro/i_execution_system.h` | Abstract execution interface | ✅ |
| `async_coro/include/async_coro/execution_queue_mark.h` | Queue identification and masking | ✅ |
| `async_coro/include/async_coro/atomic_queue.h` | Lock-free queue for task distribution | ✅ |
| `async_coro/include/async_coro/thread_notifier.h` | Efficient thread wake-up | ✅ |
| `async_coro/include/async_coro/await/` | Existing awaitables: sleep, cancel, switch_to_queue, etc. | ✅ |

---

## What's Genuinely New (Implement These)

### 3.1 `server::io::file` — Async File I/O ✅ COMPLETE

**Status:** Fully implemented and tested with 10+ unit tests.

**Files:**
- `examples/server/lib/include/server/io/file.h` — Header ✅
- `examples/server/lib/src/io/file.cpp` — Implementation (epoll/kqueue backend) ✅
- `examples/server/lib/include/server/io/file_open_mode.h` — Open mode flags ✅
- `examples/server/lib/src/io/file_open_mode.cpp` — Mode implementation ✅

**Tests:** `examples/server/tests/src/test_async_file_io.cpp` — 10+ tests covering:
- open_and_read_file, write_and_read_file, read_nonexistent_file, close_file
- read_empty_file, get_size_returns_correct_value, read_all_reads_entire_file
- read_all_empty_file, seek_moves_position, seek_error_invalid_whence

**API:**
```cpp
namespace server::io {

class file {
 public:
  static expected<file, std::string> open(reactor& reactor, const std::string& path, file_open_mode mode) noexcept;
  async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);
  async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> data);
  expected<void, std::string> flush() const;
  void close() noexcept;
  [[nodiscard]] bool is_closed() const noexcept;
  [[nodiscard]] file_handle_t get_fd() const noexcept;
  expected<size_t, std::string> get_size() const;
  expected<off_t, std::string> seek(off_t offset, seek_whence whence) const;
  async_coro::task<expected<std::vector<std::byte>, std::string>> read_all();
};

}  // namespace server::io
```

**Implementation approach:**
- Uses existing `reactor` pattern from `server::socket_layer` (epoll/kqueue)
- File descriptors are pollable on Linux (epoll supports regular files)
- Non-blocking I/O with `await_callback_with_result` pattern

**API:**
```cpp
namespace server::io {

class file {
 public:
  static expected<file, std::string> open(reactor& reactor, const std::string& path, int mode, int permissions = 0644) noexcept;
  async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);
  async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> data);
  expected<void, std::string> flush();
  void close() noexcept;
  expected<size_t, std::string> get_size();
  expected<off_t, std::string> seek(off_t offset, seek_whence whence);
  async_coro::task<expected<std::vector<std::byte>, std::string>> read_all();
};

}  // namespace server::io
```

**Implementation approach:**
- Uses existing `reactor` pattern from `server::socket_layer` (epoll/kqueue)
- File descriptors are pollable on Linux (epoll supports regular files)
- Non-blocking I/O with `await_callback_with_result` pattern

### 3.2 `server::io::io_uring_reactor` — io_uring Backend ✅ COMPLETE

**Status:** Fully implemented and tested with 8+ unit tests. Linux 5.1+ only.

**Files:**
- `examples/server/lib/include/server/io/io_uring_reactor.h` — Reactor header ✅
- `examples/server/lib/src/io/io_uring_reactor.cpp` — Implementation ✅
- `examples/server/lib/include/server/io/io_uring_file.h` — io_uring file class ✅
- `examples/server/lib/src/io/io_uring_file.cpp` — File implementation ✅

**Tests:** `examples/server/tests/src/io_uring_file_tests.cpp` — 8+ tests covering:
- open_and_close, read_file_content, write_to_file, get_file_size
- seek_and_read, read_empty_file, read_closed_file

**API:**
```cpp
namespace server::io {

class io_uring_reactor {
 public:
  static expected<io_uring_reactor, std::string> create(size_t ring_size = 256) noexcept;
  void process_loop(std::chrono::nanoseconds max_wait);
  void submit_read(int file_descriptor, uint64_t offset, std::span<uint8_t> buffer, continue_size_callback_t&& callback);
  void submit_write(int file_descriptor, uint64_t offset, std::span<const uint8_t> buffer, continue_size_callback_t&& callback);
  void submit_fsync(int file_descriptor, continue_void_callback_t&& callback);
  void submit_close(int file_descriptor, continue_void_callback_t&& callback);
  void submit_open(const char* path, int flags, int mode, continue_file_callback_t&& callback);
};

class io_uring_file {
 public:
  static async_coro::task<expected<io_uring_file, std::string>> open_coro(io_uring_reactor& reactor, std::string path, file_open_mode mode, int permissions = 0644) noexcept;
  async_coro::task<expected<size_t, std::string>> read(std::span<uint8_t> buffer);
  async_coro::task<expected<void, std::string>> write(std::span<const uint8_t> data);
  async_coro::task<expected<void, std::string>> flush();
  async_coro::task<expected<void, std::string>> close();
  expected<size_t, std::string> get_size() const;
  expected<off_t, std::string> seek(off_t offset, seek_whence whence) const;
  [[nodiscard]] bool is_closed() const noexcept;
};

}  // namespace server::io
```

**Notes:**
- io_uring works with both sockets AND regular files — making `server::io::io_uring_file` comprehensive on Linux 5.1+
- epoll/kqueue fallback remains for non-Linux platforms via `server::io::file`
- This is an **optimization** over the existing epoll reactor, not a replacement of the abstraction
- Uses conditional compilation (`#if IO_URING_ENABLED`) for platform support

### 3.3 `server::io::reactor` — Common Event Reactor ✅ COMPLETE

**Status:** Fully implemented. Unified epoll/kqueue backend for both sockets and files.

**Files:**
- `examples/server/lib/include/server/io/reactor.h` — Common reactor interface ✅
- `examples/server/lib/src/io/reactor.cpp` — epoll/kqueue implementation ✅

**API:**
```cpp
namespace server::io {
class reactor {
 public:
  enum class connection_state : uint8_t { available_read, available_write, closed };
  void process_loop(std::chrono::nanoseconds max_wait);
  size_t add_fd(file_handle_t file_descriptor);           // For regular files
  size_t add_sock(socket_type sock);                      // For sockets
  void remove_fd(file_handle_t file_descriptor, size_t index);
  void remove_sock(socket_type sock, size_t index);
  void continue_after_read_data(file_handle_t fd, size_t idx, continue_callback_t&& cb);
  void continue_after_write_data(file_handle_t fd, size_t idx, continue_callback_t&& cb);
  void continue_after_receive_data(socket_type sock, size_t idx, continue_callback_t&& cb);
  void continue_after_sent_data(socket_type sock, size_t idx, continue_callback_t&& cb);
};
}  // namespace server::io
```

### 3.4 `server::socket_layer::reactor` — Socket Reactor Wrapper ✅ COMPLETE

**Status:** Wraps common `io::reactor` for socket-specific operations.

**Files:**
- `examples/server/lib/include/server/socket_layer/reactor.h` — Socket reactor wrapper ✅
- `examples/server/lib/src/socket_layer/reactor.cpp` — Implementation ✅

Delegates all I/O polling to the common `io::reactor`, providing socket-specific interface with `connection_id` abstraction.

### 3.5 `server::socket_layer::connection` — TCP Connection ✅ COMPLETE

**Status:** Fully implemented with async read/write and SSL support.

**Files:**
- `examples/server/lib/include/server/socket_layer/connection.h` — Connection class ✅
- `examples/server/lib/src/socket_layer/connection.cpp` — Implementation ✅

Implements `core::i_read_connection` and `core::i_write_connection` interfaces. Supports non-blocking read/write with SSL handshake state machine.

### 3.6 `server::socket_layer::listener` — TCP Listener ✅ COMPLETE

**Status:** Fully implemented with accept loop and connection distribution.

**Files:**
- `examples/server/lib/include/server/socket_layer/listener.h` — Listener class ✅
- `examples/server/lib/src/socket_layer/listener.cpp` — Implementation ✅

Handles socket creation, bind, listen, accept with round-robin distribution to reactor threads.

### 3.7 `server::tcp_server` — Multi-Reactor Server ✅ COMPLETE

**Status:** Fully implemented with signal handling and reactor thread pool.

**Files:**
- `examples/server/lib/src/tcp_server.cpp` — Server implementation ✅

Multi-reactor TCP server with:
- Configurable number of reactor threads
- Signal handling (SIGINT/SIGTERM)
- SSL context management
- Connection distribution via round-robin
- Keep-alive and connection lifecycle management

### 3.8 Keep I/O Code in `server::` Namespace

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
│   ├── io/                             ← ✅ COMPLETE: Common I/O namespace
│   │   ├── reactor.h                   ← epoll/kqueue event reactor (common)
│   │   ├── file.h                      ← Async file operations (epoll backend)
│   │   ├── file_open_mode.h            ← File open mode flags
│   │   ├── io_uring_reactor.h          ← io_uring event reactor (Linux 5.1+)
│   │   ├── io_uring_file.h             ← Async file operations (io_uring backend)
│   │   └── io_config.h                 ← Platform detection macros
│   ├── socket_layer/                   ← ✅ COMPLETE: TCP networking
│   │   ├── reactor.h                   ← Socket reactor wrapper
│   │   ├── connection.h                ← TCP socket with async read/write
│   │   ├── connection_id.h             ← Platform-abstracted socket FD
│   │   ├── listener.h                  ← TCP listener for accepting connections
│   │   ├── ssl_connection.h            ← OpenSSL TLS wrapper
│   │   └── ssl_context.h               ← SSL context management
│   ├── http1/                          ← ✅ COMPLETE: HTTP/1.1 server and client
│   │   ├── http_server.h               ← HTTP server with routing
│   │   ├── http_client.h               ← HTTP client for request/response
│   │   ├── request.h / response.h      ← HTTP request/response parsing
│   │   ├── router.h                    ← URL routing
│   │   ├── compression_negotiation.h   ← gzip/br/zstd negotiation
│   │   ├── client_request.h            ← Client request builder
│   │   ├── client_response.h           ← Client response parser
│   │   ├── forwarding_params.h         ← Proxy forwarding parameters
│   │   ├── headers_holder.h            ← Headers container
│   │   ├── http_method.h               ← HTTP method enum
│   │   ├── http_status_code.h          ← Status code enum
│   │   ├── http_version.h              ← HTTP version enum
│   │   ├── http_error.h                ← HTTP error types
│   │   ├── http_server_config.h        ← Server configuration
│   │   ├── session.h                   ← HTTP session management
│   │   └── headers_type.h              ← Headers type definitions
│   ├── core/                           ← ✅ COMPLETE: Connection interfaces
│   │   ├── i_read_connection.h         ← Abstract read interface
│   │   ├── i_write_connection.h        ← Abstract write interface
│   │   └── headers_type.h              ← Headers type definitions
│   └── utils/                          ← ✅ COMPLETE: expected, compression, etc.
├── src/
│   ├── io/                             ← ✅ COMPLETE: I/O implementations
│   │   ├── reactor.cpp                 ← epoll/kqueue implementation
│   │   ├── file.cpp                    ← File I/O implementation
│   │   ├── file_open_mode.cpp          ← Mode implementation
│   │   ├── io_uring_reactor.cpp        ← io_uring implementation
│   │   ├── io_uring_file.cpp           ← io_uring file implementation
│   │   └── io_config.cpp               ← Config implementation
│   ├── socket_layer/                   ← ✅ COMPLETE: Socket implementations
│   │   ├── reactor.cpp                 ← Socket reactor implementation
│   │   ├── connection.cpp              ← Non-blocking read/write with SSL
│   │   ├── listener.cpp                ← Socket creation, bind, listen, accept
│   │   ├── ssl_connection.cpp          ← SSL handshake implementation
│   │   └── ssl_context.cpp             ← SSL context management
│   ├── http1/                          ← ✅ COMPLETE: HTTP implementations (14 .cpp files)
│   │   ├── http_server.cpp             ← Server implementation
│   │   ├── http_client.cpp             ← Client implementation
│   │   ├── request.cpp / response.cpp  ← HTTP parsing/serialization
│   │   ├── router.cpp                  ← Routing implementation
│   │   ├── compression_negotiation.cpp ← Compression negotiation
│   │   ├── client_request.cpp          ← Client request building
│   │   ├── client_response.cpp         ← Client response parsing
│   │   ├── forwarding_params.cpp       ← Forwarding parameters
│   │   ├── headers_holder.cpp          ← Headers container
│   │   ├── http_method.cpp             ← HTTP method enum
│   │   ├── http_status_code.cpp        ← Status code enum
│   │   ├── http_version.cpp            ← HTTP version enum
│   │   └── session.cpp                 ← Session management
│   └── tcp_server.cpp                  ← ✅ COMPLETE: Multi-reactor server
```

### Test Files

```
examples/server/tests/src/
├── test_async_file_io.cpp              ← ✅ 10+ tests for epoll file I/O
├── io_uring_file_tests.cpp             ← ✅ 8+ tests for io_uring file I/O
├── client_integration_tests.cpp        ← ✅ HTTP client parsing tests
├── http_keepalive_tests.cpp            ← ✅ Keep-alive behavior tests
├── web_socket_integration_tests.cpp    ← ✅ WebSocket integration tests
├── web_socket_deflate_tests.cpp        ← ✅ WebSocket deflate tests
├── proxy_integration_tests.cpp         ← ✅ Proxy forwarding tests
├── request_parser_tests.cpp            ← ✅ HTTP request parsing
├── client_parser_tests.cpp             ← ✅ HTTP client response parsing
├── http_encoding_tests.cpp             ← ✅ Encoding tests
├── base64_tests.cpp                    ← ✅ Base64 encoding tests
├── sha1_tests.cpp                      ← ✅ SHA1 hash tests
├── zlib_compress_tests.cpp             ← ✅ zlib compression tests
├── zstd_compress_tests.cpp             ← ✅ zstd compression tests
├── brotli_compress_tests.cpp           ← ✅ Brotli compression tests
└── web_socket_tests.cpp                ← ✅ WebSocket frame tests
```

---

## Testing Plan (Refined)

### Test Files and Coverage

| Test File | Tests | Status |
|-----------|-------|--------|
| `examples/server/tests/src/test_async_file_io.cpp` | 10+ tests (open, read, write, close, seek, read_all, error handling) | ✅ Complete |
| `examples/server/tests/src/io_uring_file_tests.cpp` | 8+ tests (open, read, write, flush, seek, empty file, closed file) | ✅ Complete |
| `examples/server/tests/src/client_integration_tests.cpp` | Request/response parsing | ✅ Complete |
| `examples/server/tests/src/http_keepalive_tests.cpp` | 4+ tests (timeout, max_requests, close header) | ✅ Complete |
| `examples/server/tests/src/web_socket_integration_tests.cpp` | Handshake, text echo, error cases | ✅ Complete |
| `examples/server/tests/src/web_socket_deflate_tests.cpp` | 10+ tests (various window sizes, context takeover) | ✅ Complete |
| `examples/server/tests/src/proxy_integration_tests.cpp` | RFC 7239 Forwarded header tests | ✅ Complete |
| `examples/server/tests/src/zlib_compress_tests.cpp` | Compression round-trip tests | ✅ Complete |
| `examples/server/tests/src/zstd_compress_tests.cpp` | Compression round-trip tests | ✅ Complete |
| `examples/server/tests/src/brotli_compress_tests.cpp` | Compression round-trip tests | ✅ Complete |

### Detailed Test Status by Category

| Test | Description | Status | Notes |
|------|-------------|--------|-------|
| `test_async_socket_connect` | Non-blocking TCP connect | ✅ Complete | Implemented in socket_layer connection tests |
| `test_async_socket_read_write` | Async read/write cycle | ✅ Complete | Tested via connection::read_buffer/write_buffer |
| `test_async_tcp_server_accept` | Server accepts connections | ⚠️ Partial | tcp_server.cpp implemented but no dedicated integration test |
| `test_async_http_client_get` | HTTP GET request/response | ⚠️ Partial | Client parsing tests exist, no full round-trip with server |
| `test_async_http_client_post` | HTTP POST with body | ⚠️ Partial | Client parsing tests exist, no full round-trip with server |
| `test_server_io_file_read_write` | Async file I/O | ✅ Complete | 10+ tests in test_async_file_io.cpp |
| `test_io_error_handling` | Error propagation | ⚠️ Partial | Basic error tests exist (nonexistent file, closed file), no comprehensive library-level suite |
| `test_io_uring_backend` | io_uring specific tests | ⚠️ Partial | 8+ functional tests exist, no performance benchmarks or edge cases |
| `test_concurrent_io` | Multiple concurrent I/O | ❌ Missing | No test for concurrent file/socket I/O across reactors |
| `test_io_with_cancel` | Cancel in-flight I/O | ❌ Missing | No tests for cancellation during async read/write |
| `test_io_with_switch_queue` | Switch queues after I/O | ❌ Missing | No tests for queue switching with I/O |

### Summary

- **Functional tests:** ✅ ~40+ tests covering core functionality
- **Integration tests:** ⚠️ 3/6 categories complete (50%)
- **Edge-case tests:** ❌ 0/5 categories missing (0%)
- **Performance benchmarks:** ❌ Missing

---

## Estimated Effort (Refined)

### Completed (4 days estimated, ~85% done)

| Component | Effort | Status |
|-----------|--------|--------|
| `server::io::file` (epoll/kqueue) | 1 day | ✅ Complete |
| `server::io::reactor` (common event loop) | 0.5 day | ✅ Complete |
| `server::io::io_uring_reactor` + `io_uring_file` | 2 days | ✅ Complete |
| `server::socket_layer` wrappers | 0.5 day | ✅ Complete |
| `server::tcp_server` (multi-reactor) | 1 day | ✅ Complete |
| Core tests (file, io_uring, HTTP, WebSocket) | 2 days | ✅ Complete |
| **Subtotal** | **~7 days** | **✅ ~85% complete** |

### Remaining Work (2-3 days estimated)

| Component | Effort | Priority |
|-----------|--------|----------|
| `test_concurrent_io` — Multiple concurrent I/O operations | 0.5 day | High |
| `test_io_with_cancel` — Cancel in-flight I/O | 0.5 day | High |
| `test_io_with_switch_queue` — Queue switching after I/O | 0.5 day | Medium |
| `test_async_tcp_server_accept` — Full integration test | 0.5 day | Medium |
| `test_async_http_client_get/post` — Round-trip tests | 0.5 day | Medium |
| `test_io_uring_backend` — Performance benchmarks | 0.5 day | Low |
| **Total remaining** | **~2-3 days** | |

### Grand Total

| Category | Effort |
|----------|--------|
| Completed | ~7 days |
| Remaining | ~2-3 days |
| **Total** | **~9-10 days** |

---

## Dependencies

- **epoll** — Linux (✅ implemented in `io::reactor`)
- **kqueue** — macOS/BSD (✅ implemented in `io::reactor`)
- **io_uring** — Linux kernel 5.1+ (✅ implemented, header-only via `liburing.h`)
- **OpenSSL** — TLS/SSL support (✅ implemented in `ssl_connection`)
- **DNS resolution** — `getaddrinfo` (POSIX, already used)

No external dependencies required beyond standard POSIX libraries and OpenSSL.

---

## Next Steps (Priority Order)

1. **High Priority** — Add concurrent I/O tests (`test_concurrent_io`)
2. **High Priority** — Add cancellation tests (`test_io_with_cancel`)
3. **Medium Priority** — Add queue switching tests (`test_io_with_switch_queue`)
4. **Medium Priority** — Add TCP server integration test (`test_async_tcp_server_accept`)
5. **Medium Priority** — Add HTTP client round-trip tests (`test_async_http_client_get/post`)
6. **Low Priority** — Add io_uring performance benchmarks (`test_io_uring_backend`)

Estimated remaining effort: **2-3 days** for complete test coverage.
