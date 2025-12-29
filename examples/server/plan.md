<!--
  Updated plan focused on adding HTTP/1.1 support to the existing TCP/SSL server in this repo.
  Key goals:
  - Keep implementation self-contained (no external HTTP libraries)
  - Reuse the current socket/reactor/SSL code
  - Produce a small, efficient HTTP/1.1 implementation suitable for examples and tests
-->

# Server example — updated plan (HTTP/1.1 focus)

Last updated: 2025-11-01

## Purpose

Add reliable, efficient HTTP/1.1 support to the existing `examples/server` project without introducing external HTTP libraries. The implementation will be compact, easy to read, testable, and reuses the repository's existing socket/reactor/SSL primitives.

Goals:
- Implement a small, robust HTTP/1.1 layer (parser, request/response model) in-repo.
- Support persistent connections (keep-alive), Content-Length bodies, and chunked transfer encoding (both for request bodies and responses where applicable).
- Reuse existing `connection` / `ssl_connection` abstractions so HTTP works over plain TCP and TLS.
- Provide a minimal router / handler API and a simple static-file handler for examples.

Non-goals for the first iteration:
- Full HTTP/2 or QUIC/HTTP3 support (follow-ups).
- Advanced HTTP features like large-scale range requests, complex caching logic, or full CGI-style extensibility — those can be added later.

## Quick contract (inputs/outputs)

- Inputs: TCP/TLS byte stream (connection), configuration (port, TLS cert/key optional), simple handler functions returning response bodies.
- Outputs: Valid HTTP/1.1 responses (status line, headers, body), correct connection lifecycle (keep-alive/close), and sensible error responses for invalid requests.
- Success: curl/wget against the example server can GET / and receive correct responses; persistent connections can serve multiple requests.

## Observations about current implementation (summary)

- Reactor-based IO: `server::socket_layer::reactor` implements readiness notifications (epoll/kqueue) and integrates with `async_coro` await callbacks.
- Connection abstraction: `server::socket_layer::connection` provides coroutine-friendly `read_buffer` and `write_buffer`, and supports an internal `ssl_connection` wrapper for TLS.
- Listener and accept loop: `server::tcp_server::serve` accepts connections on a listener and distributes connection objects to a provided callback; reactors run in dedicated threads and connection tasks can be scheduled on `async_coro::scheduler`.
- Example `main.cpp` already writes a hard-coded HTTP/1.1 response via `conn.write_buffer(...)`, which demonstrates how to send bytes back.

These existing pieces are well-suited for adding an HTTP layer on top of `connection`/`ssl_connection` without changing the low-level IO.

## Design for HTTP/1.1 integration (high level)

1. Minimal in-repo HTTP/1.1 parser
   - A small, stateful parser that can parse request-line, headers, and optionally the body. It should support incremental reads (i.e., reading from connection in chunks until full header is available).
   - Parse headers into a small dictionary-like structure and provide helper accessors (Content-Length, Transfer-Encoding, Connection).

2. Per-connection HTTP session
   - Create an `http1::connection` or `http1::session` wrapper that owns a `server::socket_layer::connection` and exposes coroutine-friendly operations:
     - `read_request()` -> returns a `http1::request` (method, path, headers, optional body stream / buffer)
     - `write_response(http1::response)` -> sends response headers and body, supporting both Content-Length and chunked encoding.
   - The session manages connection lifetimes and implements keep-alive: after finishing a request-response exchange, it loops to read the next request unless Connection: close or protocol requires shutdown.

3. Request body handling
   - For requests with Content-Length: read exactly N bytes (cooperative await reads).
   - For chunked request bodies (rare for typical examples): support incremental chunk parsing when Transfer-Encoding: chunked is present (implement only if needed for testing uploads).

4. Response body and streaming
   - Support small in-memory responses with Content-Length.
   - Provide chunked response support so handlers can stream large bodies (e.g., coroutines producing data over time).

5. Handler/Router API (simple)
   - Provide a minimal router that maps method+path (or predicate) to a handler function: `using handler_t = std::function<async_coro::task<http1::response>(const http1::request&)>;` (or a light `unique_function` wrapper to avoid std::function overhead).
   - Handlers are coroutine-based and return a response object or an error.

6. Integration with existing `tcp_server`
   - Keep `tcp_server` as-is. The `serve` callback currently receives a `socket_layer::connection`. Update example code (and provide new helper `http1::serve`) that wraps the incoming connection into an `http1::session` and schedules the request handler loop on a scheduler.
   - This keeps low-level accept/reactor code untouched and only adds higher-level wiring in examples.

7. TLS support
   - Reuse existing `ssl_connection` in `socket_layer::connection`. The `http1::session` should not care whether the underlying stream is TLS; it calls `read_buffer` and `write_buffer` as normal.

## Edge cases and important behaviors

- Partial headers (client sends headers in multiple TCP segments) — parser must support incremental accumulation.
- Pipelining: HTTP/1.1 pipelining is uncommon and tricky; initially disallow pipelined requests or process them sequentially per connection (safe default). Document the choice.
- Keep-alive: honor Connection: keep-alive (default in HTTP/1.1) and Connection: close.
- Large bodies: read in streaming/chunked fashion; avoid allocating an entire large body in memory when not necessary.
- Invalid requests: return 4xx responses and decide whether to close connection for malformed requests.

## File layout (refreshed and concrete)

The layout below reflects the existing `socket_layer` and adds a focused `http1` module with clear responsibilities. New files are marked as (new). Keep public-facing types under `include/server/http1/` and implementations under `src/http1/`.

examples/server/
- CMakeLists.txt
- plan.md (this file)
- certs/
  - generate_dev_cert.sh
- include/server/
  - socket_layer/  (existing: connection.h, listener.h, reactor.h, ssl_connection.h, ...)
  - tcp_server_config.h (existing)
  - http1/  (new)
    - request.h            // http1::request: method, target, headers, helpers
    - response.h           // http1::response: status, headers, body helpers (sync and streaming variants)
    - parser.h             // incremental HTTP/1.1 header parser and helpers
    - session.h            // http1::session: wraps socket_layer::connection and provides read_request/write_response loop
    - router.h             // small router + handler type alias (lightweight callback wrapper)
    - util.h               // small helpers (header canonicalization, percent-decode, mime types)
- src/
  - socket_layer/  (existing code)
  - http1/  (new)
    - parser.cpp
    - session.cpp
    - response.cpp
    - router.cpp
  - tcp_server.cpp (existing)
  - main.cpp (example entry; update or add simple_server.cpp showing HTTP handlers)
- examples/
  - simple_server.cpp (example wiring: tcp_server -> wrap to http1::session -> router handlers)
- tests/
  - http1_parser_tests.cpp  (happy path + boundary cases)
  - http1_session_tests.cpp (small integration tests using a mock connection or local socket)

Notes:
- Keep the `http1` module header-only where small helpers make sense, but place parsing and session logic in .cpp files to avoid compile-time bloat.

## Incremental implementation steps (concrete)

1. Parser + unit tests (1–2 days)
   - Implement `parser.h/cpp` that can incrementally parse request-line + headers and return the number of bytes consumed and parsed header map.
   - Add unit tests for partial header splits, large headers, folded/invalid headers.

2. Request/Response model and session skeleton (1 day)
   - Implement `request.h/response.h` and `session.h` that can read headers and, for simple GETs, return a response.
   - Provide a loop in `session` that applies a handler and writes the response; honor Connection header.
   - Update `examples/server/src/main.cpp` (or add `simple_server.cpp`) to use the new session and handler API.

3. Body handling & chunked responses (1–2 days)
   - Implement reading request bodies with Content-Length and basic chunked request parsing (optional for v1, but add support for chunked responses to allow streaming handlers).
   - Add API for handlers to stream a response (callback or coroutine producer supplying chunks).

4. TLS validation (small) and example (half day)
   - Verify existing TLS code is usable with `http1::session` (it should be). Add an example that runs TLS-enabled server using existing config and certs.

5. Tests and integration (1 day)
   - Add straightforward integration tests using `curl -v` in CI or small C++ test clients; add unit tests for parser and session edge cases.

6. Document and tidy (half day)
   - Update README and `examples/server` README snippet showing how to build/run example and how to curl/test.

## Tests and validation plan

- Unit tests for parser (partial header/CRLF splits, invalid lines, header-case handling).
- Integration test: start example server on a random port, perform sequential GETs on same connection to verify keep-alive and multiple requests.
- TLS integration test: same as above but with TLS enabled and curl -k.

## Suggested API sketch (minimal)

// pseudocode headers (not code to include here, only design)

- http1::request { method, target, version, headers, optional content-length }
- http1::response { status_code, reason, headers, body_bytes | body_stream }
- http1::session(session_opts, socket_layer::connection) {
    co_await session.process(handler);
  }

Handlers return `http1::response` or stream via a provided callback.

## Performance considerations

- Reuse single fixed-size read buffer per session and parse in-place to avoid copying where possible.
- For static files: use sendfile (platform dependent) later as an optimization; initially rely on write loops.
- Avoid heap fragmentation in hot-paths (prefer small stack-allocated buffers or arena for parsing headers).

---
