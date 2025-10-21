# async_coro Server example

This folder contains an example Server subproject for testing `async_coro` primitives.

Build
-----

From the repository root run (example uses CMake and ninja):

```bash
mkdir -p build && cd build
cmake -S . -B . -G Ninja -DASYNC_CORO_BUILD_EXAMPLES=ON
ninja -C . examples_server
```

Run
---

From the examples/server build output you can run the `simple_server` binary:

```bash
./examples/server/simple_server 8080
```

Then, in another shell:

```bash
curl http://localhost:8080/
```

Notes
-----

- This is a scaffold. Next steps are to replace the blocking accept/send loop with non-blocking sockets integrated with `async_coro`.
- TLS support and HTTP/2 are planned; see `plan.md` for details.
