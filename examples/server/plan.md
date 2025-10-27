# Server example — implementation plan

Goal
----

Create a minimal but extensible example "Server" subproject that demonstrates and tests the `async_coro` coroutine library by implementing a low-level, coroutine-friendly web server. The server will:

- Use low-level sockets (TCP/UDP) and a small portable async I/O layer.
- Support TLS via OpenSSL (server-side) with ALPN for version negotiation.
- Provide support for HTTP/1.1, HTTP/2, and HTTP/3 (QUIC) progressively.
- Be buildable from scratch via CMake with optional FetchContent for third-party libraries.

Assumptions
-----------

- The host machine has standard development tools (gcc/clang, CMake, make/ninja).
- OpenSSL development headers/libraries can be built or installed (or fetched and built by CMake).
- HTTP/2 and HTTP/3 stacks (nghttp2, msquic) can be optionally fetched / built; HTTP/3 may be deferred if the environment is constrained.
- We will reuse your `async_coro` primitives (coroutines, scheduling) as the core async primitives.

Contract (tiny)
---------------

- Inputs: TCP connections on a configurable port (IPv4/IPv6), TLS certificates (PEM), configuration flags to enable http1/http2/http3 and concurrency limits.
- Outputs: Serve HTTP responses (static or programmatically generated), logs, and diagnostics. Error modes include invalid requests, failed TLS handshake, and backend failures.
- Success criteria: A local client can connect and receive valid HTTP responses for HTTP/1.1 and HTTP/2; TLS handshake succeeds with ALPN; QUIC/HTTP3 support is optional and flagged as a milestone.

Architecture overview
---------------------

Layered design — small, testable components:

1. Platform layer
   - Minimal portability wrappers (POSIX sockets) with compile-time switches for epoll/kqueue.
   - Socket utilities: address parsing, set_nonblocking, TCP_FASTOPEN option toggles, reuseaddr.

2. I/O reactor
   - An event loop integration that cooperates with `async_coro` (either by posting readiness events into the coroutine scheduler or by providing awaitable prims for read/write/accept).
   - Use epoll on Linux (default), with a fallback to poll/select for portability.

3. Socket primitives
   - Listener/Acceptor: non-blocking accept with backlog handling.
   - Connection: read/write with scatter/gather, proper shutdown, and graceful close.

4. TLS stream wrapper (OpenSSL)
   - TLSStream wraps a Connection and implements handshake, read, write using OpenSSL BIO or non-blocking SSL functions.
   - ALPN negotiation: supply "http/1.1", "h2", and "h3" as needed.

5. HTTP layer
   - HTTP/1.1 handler: lightweight parser (tiny in-repo) with support for persistent connections, chunked transfer, and pipelining.
   - HTTP/2 handler: integrate `nghttp2` to manage frames and streams, map streams to coroutine tasks.
   - HTTP/3 handler (QUIC): `msquic` (UDP + QUIC stack). This is the most complex piece and will be a separate milestone.

6. Application handler
   - A simple request dispatcher that calls coroutine-based handlers for each request and returns responses.
   - A static-file responder for testing and a small dynamic route (e.g., /echo, /delay) for exercising coroutines and scheduling.

7. Testing & examples
   - Unit tests for socket/TLS/HTTP layers.
   - Small client examples (a tiny C++ client using sockets or msquic test client bindings).

Dependencies and Build Strategy
-------------------------------

- Core: C++20 (or newer), CMake 3.16+.
- TLS: OpenSSL (recommended OpenSSL 1.1.1+ or 3.x)
- HTTP/2: nghttp2 (via FetchContent)
- HTTP/3/QUIC: msquic or similar (via FetchContent)
- Parser: small in-project parser for HTTP/1.1

Build integration (CMake)
------------------------

- Add `examples/server/CMakeLists.txt` that defines a subproject `server` with options:
  - SERVER_ENABLE_HTTP2 (ON/OFF)
  - SERVER_ENABLE_HTTP3 (OFF by default)
  - SERVER_ENABLE_TLS (ON/OFF)
  - SERVER_BUILD_EXAMPLES/TESTS
- Prefer FetchContent for nghttp2, and msquic to keep the repo self-contained when needed.
- Create targets:
  - server (executable)
  - server_test (unit tests linking googletest)

File layout (suggested)
----------------------

examples/server/
├─ CMakeLists.txt
├─ plan.md
├─ certs/ (sample dev cert/key and helper script to generate via openssl)
├─ include/server/
│  ├─ listener.h
  ├─ connection.h
  │  ├─ tls_stream.h
  │  ├─ http1.h
  │  ├─ http2.h
  │  └─ http3.h (optional)
├─ src/
│  ├─ listener.cpp
│  ├─ connection.cpp
│  ├─ tls_stream.cpp
│  ├─ http1.cpp
│  └─ http2.cpp
├─ examples/
│  └─ simple_server.cpp (entrypoint wiring async_coro and handler)
└─ tests/
   ├─ socket_tests.cpp
   ├─ tls_tests.cpp
   └─ http_tests.cpp

Milestones and roadmap
----------------------

Phase 0 — Planning (this file)

Phase 1 — Minimal HTTP/1.1 over plain TCP (1–2 days)
- Implement platform socket wrappers and a small reactor + awaitables integrated with `async_coro`.
- Implement a simple HTTP/1.1 parser and mapping to coroutine handlers.
- Acceptance: `examples/server/simple_server` can accept connections and respond with a static "Hello" to HTTP/1.1 GET requests.

Phase 2 — TLS via OpenSSL + ALPN for HTTP/1.1 (1–2 days)
- Implement `tls_stream` using OpenSSL non-blocking APIs or SSL BIO pairing with the socket.
- Support certificate and key configuration and sample dev cert.
- Acceptance: `curl -k https://localhost:port/` returns the Hello response.

Phase 3 — HTTP/2 support (3–5 days)
- Integrate `nghttp2` and ALPN negotiation for "h2".
- Map HTTP/2 streams to coroutine tasks; implement flow control basics.
- Acceptance: `curl --http2 -k https://localhost:port/` works and multiple concurrent requests are handled on one connection.

Phase 4 — Tests, hardening and performance (ongoing)
- Add unit tests, integration tests, fuzzing entrypoints for parsers, bench/throughput tests.

Phase 5 — HTTP/3 / QUIC proof-of-concept (optional / later, 1+ weeks)
- Add QUIC stack (msquic) and implement UDP server entrypoint for QUIC connections.
- Implement minimal HTTP/3 framing to respond to GET.
- Acceptance: HTTP/3 GET returns a correct response from a compatible client.

Security considerations
-----------------------

- Provide sample scripts for creating development certs and explain production key handling.
- Configure secure defaults for OpenSSL (cipher list, TLSv1.2+ minimum, ECDHE preference).
- Support CRL/OCSP stapling later if needed.
- Rate limiting, DoS protection, and limits on concurrent streams/requests must be configurable.

Testing strategy
----------------

- Unit tests: parser, socket wrappers, TLS handshake flows (mock sockets where helpful).
- Integration tests: server binary + curl/nghttp2 client to exercise HTTP/1.1 and HTTP/2.
- CI: add CMake tests to your existing test runner so `build/` can run `ctest`.
- Fuzzing: target the HTTP/1.1 parser and HTTP/2 frame handler.

Developer UX and examples
-------------------------

- Provide `examples/server/simple_server` which is the minimal runnable server that accepts a port, toggles TLS, and responds to /, /echo and /delay routes.
- Document how to generate/dev certs in `certs/README.md` and provide a run command example.

Next steps (actionable)
-----------------------

1. Create the `examples/server` CMake scaffolding and sample dev certs.
2. Implement the socket and reactor layer and a tiny HTTP/1.1 handler.
3. Add OpenSSL TLS wrapper and ALPN support.
4. Integrate HTTP/2 via nghttp2 (optional: use system package first).
5. Consider HTTP/3/QUIC as a follow-up milestone.

Acceptance criteria for this plan
--------------------------------

- `examples/server/plan.md` exists and describes the implementation path, file layout, build strategy, and milestones.
- The next developer task is to scaffold the project (CMake + basic layout) and implement the socket layer.


---

Last updated: 2025-10-16
