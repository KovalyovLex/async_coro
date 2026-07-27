# Async Cororo Repository – Copilot Instructions

These notes are for an AI coding agent working on the **async_coro** project.  They collect the
important domain knowledge, conventions and workflows that let you be productive immediately.

---

## 1. Big‑picture architecture

* The project is a **small C++20 library** that implements an asynchronous execution system
  around C++20 coroutines.  The public API lives under `async_coro/include/async_coro` and the
  implementation is in `async_coro/src`.
* Core concepts: **`task` / `task_handle` / `promise_result`** (coroutine wrappers),
  **`scheduler`** (drives a coroutine to completion) and **`execution_system`** (thread‑pool style
  queues, queue marks, masks, `executor_data` to identify the calling thread).  There is also a
  lightweight `atomic_queue` and various wait/notify utilities.
* The library is not header‑only; it builds as a `async_coro` static/shared target in CMake.
* Tests live under `tests/` with a `common` directory plus `simple_tests` and
  `long_runnung_tests`.  Android variants are built when the `ANDROID` CMake variable is set.
* Examples are under `examples/` and are enabled by the `ASYNC_CORO_EXAMPLES_ENABLED` option.

When you modify or add new functionality, look for existing files with the same
responsibility (`scheduler.cpp`, `execution_system.cpp`, etc.) and follow the model there.

## 2. Build and developer workflows

1. **Configuration** – always use CMake (3.31+).  Most developers use the
   [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
   extension; manual `cmake` commands are discouraged (CI also just runs CMake directly).
2. **Options of interest** (pass via `-D` to CMake):
   * `ASYNC_CORO_ASAN_ENABLED` / `ASYNC_CORO_TSAN_ENABLED` – enable sanitizers globally.
   * `ASYNC_CORO_NO_EXCEPTIONS` – builds the library with exceptions disabled.  CI exercises
     both modes.
   * `ASYNC_CORO_TESTS_ENABLED` / `ASYNC_CORO_EXAMPLES_ENABLED` – toggle subdirectories.
   * `ASYNC_CORO_TEST_KEEP_DEBUG_SYMBOLS` – used by long‑running tests for symbol lookup.
3. **Building** – run `cmake -Bbuild -H.` then `cmake --build build --parallel` (or use the
   CMake Tools build task).  A Ninja build directory is kept in `build/` by default.
4. **Testing** – tests compile into `tests_simple` and `tests_long` executables (or a shared
   library on Android).  They are executed by CTest or the helper script
   `.github/scripts/run_tests.sh` which exercises repeat loops and signal handling.  Use
   `RunCtest_CMakeTools` or `cmake --build build --target test` when iterating locally.
   * `./tests/tests_simple --gtest_repeat=30` is the usual fast sequence.
   * Long tests have a 120‑second timeout in CTest; `--gtest_brief=1` is used in CI.
5. **Sanity checks** – CI also runs a lint workflow (`.github/workflows/cpp-linter.yml`) which
   invokes clang‑tidy/format; local development should run the same via the CMake commands or
   your editor integration.

---

## C++ Coding Standards (from project instructions)

Refer to `.claude/cpp_coding_instructions.instructions.md` for full details, but key points are:

- Use snake_case names, lowercase filenames with underscores.
- Document public APIs with Doxygen comments (`/** ... */`) including `@param`, `@return`, etc.
- Follow formatting rules: 2-space indent, braces on same line, `noexcept` where applicable, `[[nodiscard]]` on results, etc.
- Optimize for performance: minimize allocations, avoid virtual dispatch, use `std::string_view`/`std::span` and branch hints.
- Avoid manual `cmake` calls; rely on CMake Tools extension and run clang-tidy/clang-format through CMake.
- Ensure tests accompany new features and run `clang-tidy`/format before PRs.
