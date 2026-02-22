---
name: "C++ Coding Instructions"
description: "Standards and guidelines for C++ source files in the async_coro project."
applyTo: "**/*.{cpp,h,hpp}"
---

# Async Coro C++ Coding Standards

This document describes the conventions and expectations for all C++ source files in the `async_coro` repository.  It applies equally to `.cpp`, `.h`, and `.hpp` files and is intended to keep the high‑performance, modern‑C++ codebase consistent across all contributors.

---

## 1. General Philosophy 💡

- **C++20** is the baseline standard.  Use language features (concepts, `[[likely]]`, `std::span`, `std::byte`, `requires`, etc.) when they improve clarity or performance.
- The library is designed for **low‑overhead, high‑throughput** scenarios.  A significant portion of the code is performance‑sensitive; avoid unnecessary allocations, virtual dispatch, or exception handling in hot paths.
- Readability and correctness are still important.  Use modern tooling (clang‑tidy, clang‑format) to maintain quality automatically.

---

## 2. Tooling & Build Checks 🔧

- A `.clang-tidy` file already lives at the project root with a strict configuration.  All new code must compile cleanly with the same settings.  Run clang‑tidy via the CMake integration or your editor (see below).
- Use **clang-format** (the project provides a `.clang-format` file) before committing. Many editors/IDE plugins will format on save.
- **Do not invoke `cmake` manually in scripts or CI**.  Instead, rely on the [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) extension or equivalent integrations.  Those tools automatically configure, build, and run checks using the workspace's CMake configuration and respect generator choices.
- Tests are managed with CTest; use the CMake extension's test runner or `RunCtest_CMakeTools` to execute them.

> ⚠️ The absence of manual cmake calls helps keep local builds aligned with CI and avoids environment drift.

---

## 3. File and Identifier Naming 📂

- **Files**: lower‑case, underscore separated (`scheduler.cpp`, `task_handle.h`).
- **Namespaces**: mirror the directory structure; typically `async_coro` or `async_coro::internal`, `server::http1`, etc.
- **Classes / structs / concepts**: lower‑case with underscores (`scheduler`, `task_handle`, `passkey`).  There are no upper‑camel‑case types in the core library.
- **Functions and methods**: snake_case (`start_task`, `continue_execution_impl`).
- **Variables / data members**: snake_case with leading underscore for private members (`_mutex`, `_managed_coroutines`).
- **Macros**: upper‑case with `ASYNC_CORO_` prefix when global; otherwise normal macro conventions.
- **Constants / enum values**: upper‑case or `kCamelCase` is acceptable; be consistent within a file.

---

## 4. Formatting & Style 🧩

- **Indentation**: 2 spaces per level.  Never use tabs.
- **Braces**: opening brace on same line as the declaration (`if (cond) {`), closing brace on its own line.  Single‑statement bodies may omit braces only when clang‑format makes it safe.
- **Line length**: keep to ~100 characters; long expressions are wrapped by clang‑format.
- **`[[nodiscard]]`** on functions that return non‑void where ignoring the result is likely a bug.
- **`noexcept`**: use `noexcept` on all functions that cannot throw.  In API headers, prefer `noexcept` even on small helpers.
- Use `constexpr` and `inline` for inlineable utilities and avoid ODR violations.
- Prefer `auto` when the type is obvious or verbose; otherwise specify the return type explicitly.

---

## 5. Documentation 💬

All public types, functions and significant private helpers must be documented using Doxygen‑style comments (`/** ... */`).  The repository already uses this style extensively.

Example:
```cpp
/**
 * @brief Schedules a task and starts its execution.
 *
 * This function takes a coroutine task, assigns it to an execution queue,
 * and begins its execution. If the task is already completed, it is freed
 * immediately.
 *
 * @tparam R The return type of the task.
 * @param launcher The task wrapped to task_launcher to be executed.
 * @return A handle to the started task.
 */
```

- Use `@param`, `@tparam`, `@return` tags as shown.
- Comments should reside **above** declarations and not inline unless brief (e.g. `// NOLINT(*)` or short notes).
- For internal code, simple `//` comments are fine, but prefer the same style for consistency.

---

## 6. Performance Considerations ⚡

Given the library's goals, the following guidelines apply:

1. **Minimize allocations**: prefer `std::array`, `std::vector::reserve`, or stack buffers (e.g. `std::array<std::byte, 4*1024> buffer;`).
2. **Avoid unnecessary copies**: pass by `const&` or `&&` when appropriate, use `std::string_view`/`std::span` for read‑only views.
3. **No hidden virtual calls**: polymorphism is usually handled via templates or small `std::function`‑like wrappers (`unique_function`). Avoid use of `std::function` in code (allowed only in tests).
4. **Conditional compilation**: use `[[likely]]`, `[[unlikely]]` only if you 100% sure (for example error handling) and branch hints to help the optimizer.
5. **Exceptions**: code is written to compile with exceptions disabled (see `ASYNC_CORO_WITH_EXCEPTIONS`); avoid throwing any exceptions and use `expected`/`std::optional` models.
6. **Thread safety**: preference for custom `mutex` wrappers and annotations (`CORO_THREAD_GUARDED_BY`) to keep static analysis happy.

When profiling reveals a hotspot, refactor using C++20 facilities but keep correctness first.

---

## 7. Testing and Linting ✅

- Every new feature or bug fix should come with corresponding tests under `tests/` (simple or long running).
- Tests follow the same style (snake_case filenames, Doxygen comments for helpers, `EXPECT_*` macros from GoogleTest).
- Run `clang-tidy` and `clang-format` before opening a PR.  CI will also enforce these checks.

---

## 8. Contribution Checklist 🛠️

1. Follow naming and formatting conventions.
2. Document public APIs.
3. Ensure code builds with the CMake extension (run configure/build via your IDE).
4. Run `clang-tidy` and fix reported issues.
5. Add or update tests demonstrating correctness and performance expectations.
6. Commit with clear messages and update `CHANGELOG.md` if necessary.

---

By adhering to this guide, we keep `async_coro` fast, maintainable and pleasant to work on.  Thank you for contributing! 🚀
