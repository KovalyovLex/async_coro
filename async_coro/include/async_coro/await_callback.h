#pragma once

#include <async_coro/internal/await_callback.h>

#include <utility>

namespace async_coro {
// Suspends coroutine and call continuation function with resume function as
// argument. Resume function has signature void(), its invocation immedatelly
// resumes the coroutine.
template <typename T>
auto await_callback(T continuation) {
  return internal::await_callback{std::move(continuation)};
}
}  // namespace async_coro
